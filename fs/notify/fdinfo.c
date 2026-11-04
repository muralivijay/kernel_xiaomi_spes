// SPDX-License-Identifier: GPL-2.0
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify_backend.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/inotify.h>
#include <linux/fanotify.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/exportfs.h>
#ifdef CONFIG_KSU_SUSFS
#include <linux/susfs_def.h>
#endif // #ifdef CONFIG_KSU_SUSFS

#include "inotify/inotify.h"
#include "fsnotify.h"

#if defined(CONFIG_PROC_FS)

#if defined(CONFIG_INOTIFY_USER) || defined(CONFIG_FANOTIFY)

#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
extern bool susfs_is_inode_sus_kstat(struct inode *inode, bool *out_is_fuse);
extern void susfs_sus_kstat_spoof_inotify_fdinfo(unsigned long *out_target_ino, dev_t *out_target_dev);
#endif // #ifdef CONFIG_KSU_SUSFS_SUS_KSTAT

#if defined(CONFIG_KSU_SUSFS_SUS_MOUNT) || defined(CONFIG_KSU_SUSFS_SUS_KSTAT)
static void show_fdinfo(struct seq_file *m, struct file *f,
			void (*show)(struct seq_file *m,
				     struct fsnotify_mark *mark,
					 struct file *file))
#else
static void show_fdinfo(struct seq_file *m, struct file *f,
			void (*show)(struct seq_file *m,
				     struct fsnotify_mark *mark))
#endif // #if defined(CONFIG_KSU_SUSFS_SUS_MOUNT) || defined(CONFIG_KSU_SUSFS_SUS_KSTAT)
{
	struct fsnotify_group *group = f->private_data;
	struct fsnotify_mark *mark;

	mutex_lock(&group->mark_mutex);
	list_for_each_entry(mark, &group->marks_list, g_list) {
#if defined(CONFIG_KSU_SUSFS_SUS_MOUNT) || defined(CONFIG_KSU_SUSFS_SUS_KSTAT)
		show(m, mark, f);
#else
		show(m, mark);
#endif // #if defined(CONFIG_KSU_SUSFS_SUS_MOUNT) || defined(CONFIG_KSU_SUSFS_SUS_KSTAT)
		if (seq_has_overflowed(m))
			break;
	}
	mutex_unlock(&group->mark_mutex);
}

#if defined(CONFIG_EXPORTFS)
static void show_mark_fhandle(struct seq_file *m, struct inode *inode)
{
	struct {
		struct file_handle handle;
		u8 pad[MAX_HANDLE_SZ];
	} f;
	int size, ret, i;

	f.handle.handle_bytes = sizeof(f.pad);
	size = f.handle.handle_bytes >> 2;

	ret = exportfs_encode_inode_fh(inode, (struct fid *)f.handle.f_handle, &size, 0);
	if ((ret == FILEID_INVALID) || (ret < 0)) {
		WARN_ONCE(1, "Can't encode file handler for inotify: %d\n", ret);
		return;
	}

	f.handle.handle_type = ret;
	f.handle.handle_bytes = size * sizeof(u32);

	seq_printf(m, "fhandle-bytes:%x fhandle-type:%x f_handle:",
		   f.handle.handle_bytes, f.handle.handle_type);

	for (i = 0; i < f.handle.handle_bytes; i++)
		seq_printf(m, "%02x", (int)f.handle.f_handle[i]);
}
#else
static void show_mark_fhandle(struct seq_file *m, struct inode *inode)
{
}
#endif

#ifdef CONFIG_INOTIFY_USER

#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
static void inotify_fdinfo(struct seq_file *m, struct fsnotify_mark *mark, struct file *file)
#else
static void inotify_fdinfo(struct seq_file *m, struct fsnotify_mark *mark)
#endif
{
	struct inotify_inode_mark *inode_mark;
	struct inode *inode;

	if (mark->connector->type != FSNOTIFY_OBJ_TYPE_INODE)
		return;

	inode_mark = container_of(mark, struct inotify_inode_mark, fsn_mark);
	inode = igrab(fsnotify_conn_inode(mark->connector));
	if (inode) {
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
		if (susfs_is_current_app_uid()) {
			bool is_fuse = false;
			if (susfs_is_inode_sus_kstat(inode, &is_fuse)) {
				unsigned long ino = inode->i_ino;
				dev_t dev = inode->i_sb->s_dev;
				susfs_sus_kstat_spoof_inotify_fdinfo(&ino, &dev);
				seq_printf(m, "inotify wd:%x ino:%lx sdev:%x mask:%x ignored_mask:0 ",
						inode_mark->wd, ino, dev,
						inotify_mark_user_mask(mark));
				show_mark_fhandle(m, inode);
				seq_putc(m, '\n');
				iput(inode);
				return;
			}
		}
#endif // #ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
		if (likely(susfs_is_current_proc_umounted())) {
			struct mount *mnt = real_mount(file->f_path.mnt);
			if (mnt->mnt_id >= DEFAULT_KSU_MNT_ID) {
				struct path path;
				char *pathname = kmalloc(PAGE_SIZE, GFP_KERNEL);
				char *dpath;
				if (!pathname) {
					goto orig_flow;
				}
				dpath = d_path(&file->f_path, pathname, PAGE_SIZE);
				if (!dpath) {
					goto out_kfree;
				}
				if (kern_path(dpath, 0, &path)) {
					goto out_kfree;
				}
				if (!d_backing_inode(path.dentry)) {
					goto out_path_put;
				}
				seq_printf(m, "inotify wd:%x ino:%lx sdev:%x mask:%x ignored_mask:0 ",
						inode_mark->wd, d_backing_inode(path.dentry)->i_ino, d_backing_inode(path.dentry)->i_sb->s_dev,
						inotify_mark_user_mask(mark));
				show_mark_fhandle(m, d_backing_inode(path.dentry));
				seq_putc(m, '\n');
				path_put(&path);
				kfree(pathname);
				iput(inode);
				return;
out_path_put:
				path_put(&path);
out_kfree:
				kfree(pathname);
			}
		}
orig_flow:
#endif // #ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
		seq_printf(m, "inotify wd:%x ino:%lx sdev:%x mask:%x ignored_mask:0 ",
			   inode_mark->wd, inode->i_ino, inode->i_sb->s_dev,
			   inotify_mark_user_mask(mark));
		show_mark_fhandle(m, inode);
		seq_putc(m, '\n');
		iput(inode);
	}
}

void inotify_show_fdinfo(struct seq_file *m, struct file *f)
{
	show_fdinfo(m, f, inotify_fdinfo);
}

#endif /* CONFIG_INOTIFY_USER */

#ifdef CONFIG_FANOTIFY

static void fanotify_fdinfo(struct seq_file *m, struct fsnotify_mark *mark)
{
	unsigned int mflags = 0;
	struct inode *inode;

	if (mark->flags & FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY)
		mflags |= FAN_MARK_IGNORED_SURV_MODIFY;

	if (mark->connector->type == FSNOTIFY_OBJ_TYPE_INODE) {
		inode = igrab(fsnotify_conn_inode(mark->connector));
		if (!inode)
			return;
		seq_printf(m, "fanotify ino:%lx sdev:%x mflags:%x mask:%x ignored_mask:%x ",
			   inode->i_ino, inode->i_sb->s_dev,
			   mflags, mark->mask, mark->ignored_mask);
		show_mark_fhandle(m, inode);
		seq_putc(m, '\n');
		iput(inode);
	} else if (mark->connector->type == FSNOTIFY_OBJ_TYPE_VFSMOUNT) {
		struct mount *mnt = fsnotify_conn_mount(mark->connector);

		seq_printf(m, "fanotify mnt_id:%x mflags:%x mask:%x ignored_mask:%x\n",
			   mnt->mnt_id, mflags, mark->mask, mark->ignored_mask);
	}
}

void fanotify_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct fsnotify_group *group = f->private_data;
	unsigned int flags = 0;

	switch (group->priority) {
	case FS_PRIO_0:
		flags |= FAN_CLASS_NOTIF;
		break;
	case FS_PRIO_1:
		flags |= FAN_CLASS_CONTENT;
		break;
	case FS_PRIO_2:
		flags |= FAN_CLASS_PRE_CONTENT;
		break;
	}

	if (group->max_events == UINT_MAX)
		flags |= FAN_UNLIMITED_QUEUE;

	if (group->fanotify_data.max_marks == UINT_MAX)
		flags |= FAN_UNLIMITED_MARKS;

	if (group->fanotify_data.audit)
		flags |= FAN_ENABLE_AUDIT;

	seq_printf(m, "fanotify flags:%x event-flags:%x\n",
		   flags, group->fanotify_data.f_flags);

	show_fdinfo(m, f, fanotify_fdinfo);
}

#endif /* CONFIG_FANOTIFY */

#endif /* CONFIG_INOTIFY_USER || CONFIG_FANOTIFY */

#endif /* CONFIG_PROC_FS */
