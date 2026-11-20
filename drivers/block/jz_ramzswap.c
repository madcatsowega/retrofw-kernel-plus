/*
 * drivers/block/jz_ramzswap.c
 *
 * Lightweight LZO1X compressed RAM swap block device for Linux 2.6.31.3
 * (RetroFW Kernel+ / RS-97 Plus).
 *
 * Exposes /dev/ramzswap0 and live statistics at /proc/ramzswap.
 * Activate with: mkswap /dev/ramzswap0 && swapon -p 100 /dev/ramzswap0
 *
 * GPL v2.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/lzo.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT		9
#endif

#define RZS_DEFAULT_KB		49152	/* 48 MiB virtual compressed swap */
#define RZS_MAX_COMPR_SIZE	3072	/* Store raw if LZO saves < 25% */

#define RZS_FLAG_ZERO		(1U << 0)
#define RZS_FLAG_RAW		(1U << 1)

struct rzs_slot {
	void *data;
	u16 len;
	u16 flags;
};

struct rzs_device {
	struct request_queue *queue;
	struct gendisk *disk;
	struct mutex lock;
	struct rzs_slot *table;
	unsigned long num_pages;
	void *wrkmem;
	u8 *compr_buf;
	u8 *page_buf;

	/* Statistics */
	unsigned long orig_pages;
	unsigned long zero_pages;
	unsigned long compr_pages;
	unsigned long raw_pages;
	unsigned long long compr_bytes;
	unsigned long reads;
	unsigned long writes;
	unsigned long failed_writes;
};

static unsigned long disksize_kb = RZS_DEFAULT_KB;
module_param(disksize_kb, ulong, 0444);
MODULE_PARM_DESC(disksize_kb, "Virtual size of ramzswap0 in KiB (default 49152)");

static int rzs_major;
static struct rzs_device *rzs_dev;

static int page_is_zero(const u8 *buf)
{
	const unsigned long *p = (const unsigned long *)buf;
	unsigned int i;

	for (i = 0; i < PAGE_SIZE / sizeof(unsigned long); i++) {
		if (p[i] != 0)
			return 0;
	}
	return 1;
}

static void rzs_free_slot(struct rzs_device *dev, unsigned long index)
{
	struct rzs_slot *slot = &dev->table[index];

	if (slot->flags & RZS_FLAG_ZERO) {
		dev->zero_pages--;
		dev->orig_pages--;
		slot->flags = 0;
		return;
	}

	if (slot->data) {
		if (slot->flags & RZS_FLAG_RAW)
			dev->raw_pages--;
		else
			dev->compr_pages--;
		dev->compr_bytes -= slot->len;
		dev->orig_pages--;
		kfree(slot->data);
		slot->data = NULL;
		slot->len = 0;
		slot->flags = 0;
	}
}

static int rzs_read_page_locked(struct rzs_device *dev, unsigned long index, u8 *dst)
{
	struct rzs_slot *slot = &dev->table[index];
	size_t out_len = PAGE_SIZE;
	int ret;

	if ((slot->flags & RZS_FLAG_ZERO) || !slot->data) {
		memset(dst, 0, PAGE_SIZE);
		return 0;
	}

	if (slot->flags & RZS_FLAG_RAW) {
		memcpy(dst, slot->data, PAGE_SIZE);
		return 0;
	}

	ret = lzo1x_decompress_safe(slot->data, slot->len, dst, &out_len);
	if (unlikely(ret != LZO_E_OK || out_len != PAGE_SIZE)) {
		printk(KERN_ERR "ramzswap: LZO decompress error %d on page %lu (len=%u out=%zu)\n",
		       ret, index, slot->len, out_len);
		memset(dst, 0, PAGE_SIZE);
		return -EIO;
	}
	return 0;
}

static int rzs_write_page_locked(struct rzs_device *dev, unsigned long index, const u8 *src)
{
	struct rzs_slot *slot = &dev->table[index];
	size_t clen = 0;
	void *buf;
	int ret;

	rzs_free_slot(dev, index);

	if (page_is_zero(src)) {
		slot->flags = RZS_FLAG_ZERO;
		dev->zero_pages++;
		dev->orig_pages++;
		return 0;
	}

	ret = lzo1x_1_compress(src, PAGE_SIZE, dev->compr_buf, &clen, dev->wrkmem);
	if (likely(ret == LZO_E_OK && clen > 0 && clen <= RZS_MAX_COMPR_SIZE)) {
		buf = kmalloc(clen, GFP_NOIO | __GFP_NOWARN);
		if (!buf) {
			dev->failed_writes++;
			return -ENOMEM;
		}
		memcpy(buf, dev->compr_buf, clen);
		slot->data = buf;
		slot->len = (u16)clen;
		slot->flags = 0;
		dev->compr_pages++;
		dev->compr_bytes += clen;
		dev->orig_pages++;
		return 0;
	}

	/* Incompressible page: store raw */
	buf = kmalloc(PAGE_SIZE, GFP_NOIO | __GFP_NOWARN);
	if (!buf) {
		dev->failed_writes++;
		return -ENOMEM;
	}
	memcpy(buf, src, PAGE_SIZE);
	slot->data = buf;
	slot->len = PAGE_SIZE;
	slot->flags = RZS_FLAG_RAW;
	dev->raw_pages++;
	dev->compr_bytes += PAGE_SIZE;
	dev->orig_pages++;
	return 0;
}

static int rzs_bvec_rw(struct rzs_device *dev, struct bio_vec *bvec,
		       unsigned long index, unsigned int offset, int rw)
{
	u8 *user_mem;
	int err = 0;

	if (index >= dev->num_pages)
		return -EIO;

	mutex_lock(&dev->lock);

	if (rw == READ) {
		dev->reads++;
		err = rzs_read_page_locked(dev, index, dev->page_buf);
		if (!err) {
			user_mem = kmap_atomic(bvec->bv_page, KM_USER0);
			memcpy(user_mem + bvec->bv_offset,
			       dev->page_buf + offset, bvec->bv_len);
			kunmap_atomic(user_mem, KM_USER0);
			flush_dcache_page(bvec->bv_page);
		}
	} else {
		dev->writes++;
		if (offset != 0 || bvec->bv_len != PAGE_SIZE) {
			/* Partial-page write (e.g. mkswap header): read-modify-write */
			err = rzs_read_page_locked(dev, index, dev->page_buf);
			if (err)
				goto out_unlock;
		}
		user_mem = kmap_atomic(bvec->bv_page, KM_USER0);
		memcpy(dev->page_buf + offset,
		       user_mem + bvec->bv_offset, bvec->bv_len);
		kunmap_atomic(user_mem, KM_USER0);
		err = rzs_write_page_locked(dev, index, dev->page_buf);
	}

out_unlock:
	mutex_unlock(&dev->lock);
	return err;
}

static int rzs_make_request(struct request_queue *q, struct bio *bio)
{
	struct rzs_device *dev = q->queuedata;
	struct bio_vec *bvec;
	sector_t sector = bio->bi_sector;
	int rw = bio_rw(bio);
	int i, err = 0;

	if (rw == READA)
		rw = READ;

	if (sector + (bio->bi_size >> SECTOR_SHIFT) > get_capacity(dev->disk)) {
		err = -EIO;
		goto out;
	}

	bio_for_each_segment(bvec, bio, i) {
		unsigned int bv_len = bvec->bv_len;
		unsigned int bv_off = bvec->bv_offset;

		while (bv_len > 0) {
			unsigned long index = (unsigned long)(sector >> (PAGE_SHIFT - SECTOR_SHIFT));
			unsigned int pg_off = (unsigned int)((sector & ((PAGE_SIZE >> SECTOR_SHIFT) - 1)) << SECTOR_SHIFT);
			unsigned int chunk = min(bv_len, (unsigned int)(PAGE_SIZE - pg_off));
			struct bio_vec sub = {
				.bv_page = bvec->bv_page,
				.bv_len = chunk,
				.bv_offset = bv_off,
			};

			err = rzs_bvec_rw(dev, &sub, index, pg_off, rw);
			if (err)
				goto out;

			bv_len -= chunk;
			bv_off += chunk;
			sector += chunk >> SECTOR_SHIFT;
		}
	}

out:
	bio_endio(bio, err);
	return 0;
}

static struct block_device_operations rzs_fops = {
	.owner = THIS_MODULE,
};

static int rzs_proc_read(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	struct rzs_device *dev = rzs_dev;
	unsigned long orig_kb, compr_kb, ratio_x100 = 0;
	int len = 0;

	if (!dev)
		return 0;

	mutex_lock(&dev->lock);
	orig_kb = dev->orig_pages * (PAGE_SIZE / 1024);
	compr_kb = (unsigned long)((dev->compr_bytes + 1023) >> 10);
	if (compr_kb > 0)
		ratio_x100 = (orig_kb * 100) / compr_kb;

	len += sprintf(page + len, "DiskSize:       %lu kB\n", dev->num_pages * (PAGE_SIZE / 1024));
	len += sprintf(page + len, "OrigDataSize:   %lu kB\n", orig_kb);
	len += sprintf(page + len, "ComprDataSize:  %lu kB\n", compr_kb);
	len += sprintf(page + len, "ZeroPages:      %lu\n", dev->zero_pages);
	len += sprintf(page + len, "ComprPages:     %lu\n", dev->compr_pages);
	len += sprintf(page + len, "RawPages:       %lu\n", dev->raw_pages);
	len += sprintf(page + len, "Ratio:          %lu.%02lux\n", ratio_x100 / 100, ratio_x100 % 100);
	len += sprintf(page + len, "Reads:          %lu\n", dev->reads);
	len += sprintf(page + len, "Writes:         %lu\n", dev->writes);
	len += sprintf(page + len, "FailedWrites:   %lu\n", dev->failed_writes);
	mutex_unlock(&dev->lock);

	*eof = 1;
	return len;
}

static int __init rzs_init(void)
{
	struct rzs_device *dev;
	unsigned long num_pages;

	if (disksize_kb < 4096)
		disksize_kb = 4096;
	if (disksize_kb > 98304)
		disksize_kb = 98304;
	num_pages = disksize_kb / (PAGE_SIZE / 1024);

	rzs_major = register_blkdev(0, "ramzswap");
	if (rzs_major <= 0) {
		printk(KERN_ERR "ramzswap: register_blkdev failed\n");
		return -EBUSY;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		goto err_unreg;

	mutex_init(&dev->lock);
	dev->num_pages = num_pages;
	dev->table = vmalloc(num_pages * sizeof(struct rzs_slot));
	if (!dev->table)
		goto err_dev;
	memset(dev->table, 0, num_pages * sizeof(struct rzs_slot));

	dev->wrkmem = vmalloc(LZO1X_1_MEM_COMPRESS);
	dev->compr_buf = kmalloc(lzo1x_worst_compress(PAGE_SIZE), GFP_KERNEL);
	dev->page_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!dev->wrkmem || !dev->compr_buf || !dev->page_buf)
		goto err_bufs;

	dev->queue = blk_alloc_queue(GFP_KERNEL);
	if (!dev->queue)
		goto err_bufs;
	dev->queue->queuedata = dev;
	blk_queue_make_request(dev->queue, rzs_make_request);
	blk_queue_logical_block_size(dev->queue, PAGE_SIZE);

	dev->disk = alloc_disk(1);
	if (!dev->disk)
		goto err_queue;

	dev->disk->major = rzs_major;
	dev->disk->first_minor = 0;
	dev->disk->fops = &rzs_fops;
	dev->disk->queue = dev->queue;
	dev->disk->private_data = dev;
	snprintf(dev->disk->disk_name, sizeof(dev->disk->disk_name), "ramzswap0");
	set_capacity(dev->disk, (sector_t)num_pages << (PAGE_SHIFT - SECTOR_SHIFT));

	rzs_dev = dev;
	add_disk(dev->disk);
	create_proc_read_entry("ramzswap", 0444, NULL, rzs_proc_read, NULL);

	printk(KERN_INFO "ramzswap: initialized /dev/ramzswap0 (%lu kB virtual LZO swap)\n",
	       disksize_kb);
	return 0;

err_queue:
	blk_cleanup_queue(dev->queue);
err_bufs:
	kfree(dev->page_buf);
	kfree(dev->compr_buf);
	vfree(dev->wrkmem);
	vfree(dev->table);
err_dev:
	kfree(dev);
err_unreg:
	unregister_blkdev(rzs_major, "ramzswap");
	return -ENOMEM;
}

static void __exit rzs_exit(void)
{
	unsigned long i;

	if (!rzs_dev)
		return;

	remove_proc_entry("ramzswap", NULL);
	del_gendisk(rzs_dev->disk);
	put_disk(rzs_dev->disk);
	blk_cleanup_queue(rzs_dev->queue);

	for (i = 0; i < rzs_dev->num_pages; i++)
		rzs_free_slot(rzs_dev, i);

	kfree(rzs_dev->page_buf);
	kfree(rzs_dev->compr_buf);
	vfree(rzs_dev->wrkmem);
	vfree(rzs_dev->table);
	kfree(rzs_dev);
	rzs_dev = NULL;
	unregister_blkdev(rzs_major, "ramzswap");
}

module_init(rzs_init);
module_exit(rzs_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZO1X Compressed RAM Swap Block Device for RetroFW");
