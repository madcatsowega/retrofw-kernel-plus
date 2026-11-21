/*
 * Ingenic JZ4760B MXU SIMD register context-switch helpers.
 *
 * To preserve 100% CONFIG_MODVERSIONS / struct task_struct ABI compatibility
 * with existing RetroFW 2.3 kernel modules (g_ether.ko, g_file_storage.ko,
 * ramzswap.ko, etc.), the 64-byte MXU save area (xr1..xr15 + xr16) is placed
 * at fixed offset +128 inside each task's 8 KB thread_info/stack page (well
 * past sizeof(struct thread_info) + STACK_END_MAGIC, and >7.8 KB below top of
 * kernel stack).
 */
#ifndef _ASM_JZ_MXU_CTX_H
#define _ASM_JZ_MXU_CTX_H

#ifdef CONFIG_SOC_JZ4760B

extern void jz_mxu_save(unsigned int *xr);
extern void jz_mxu_restore(const unsigned int *xr);
extern void jz_mxu_init(unsigned int *xr);

#define JZ_MXU_STATE_OFFSET	128
#define jz_mxu_state_ptr(tsk) \
	((unsigned int *)((char *)task_thread_info(tsk) + JZ_MXU_STATE_OFFSET))

#define __save_mxu(tsk)		jz_mxu_save(jz_mxu_state_ptr(tsk))
#define __restore_mxu(tsk)	jz_mxu_restore(jz_mxu_state_ptr(tsk))
#define __init_mxu(tsk)		jz_mxu_init(jz_mxu_state_ptr(tsk))

#else

#define __save_mxu(tsk)		do { (void)(tsk); } while (0)
#define __restore_mxu(tsk)	do { (void)(tsk); } while (0)
#define __init_mxu(tsk)		do { (void)(tsk); } while (0)

#endif /* CONFIG_SOC_JZ4760B */

#endif /* _ASM_JZ_MXU_CTX_H */
