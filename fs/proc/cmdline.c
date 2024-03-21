// SPDX-License-Identifier: GPL-2.0
#include <linux/check_rdp.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/setup.h>
#ifdef CONFIG_INITRAMFS_IGNORE_SKIP_FLAG
#include <asm/setup.h>
#endif

#ifdef CONFIG_INITRAMFS_IGNORE_SKIP_FLAG
#define INITRAMFS_STR_FIND "skip_initramf"
#define INITRAMFS_STR_REPLACE "want_initramf"
#define INITRAMFS_STR_LEN (sizeof(INITRAMFS_STR_FIND) - 1)

static char proc_command_line[COMMAND_LINE_SIZE];

static void proc_command_line_init(void) {
	char *offset_addr;

	strcpy(proc_command_line, saved_command_line);

	offset_addr = strstr(proc_command_line, INITRAMFS_STR_FIND);
	if (!offset_addr)
		return;

	memcpy(offset_addr, INITRAMFS_STR_REPLACE, INITRAMFS_STR_LEN);
}
#endif

static char new_command_line[COMMAND_LINE_SIZE];

static int cmdline_proc_show(struct seq_file *m, void *v)
{
#ifdef CONFIG_INITRAMFS_IGNORE_SKIP_FLAG
	seq_printf(m, "%s\n", proc_command_line);
#else
	seq_printf(m, "%s\n", new_command_line);
#endif
	return 0;
}

static int cmdline_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cmdline_proc_show, NULL);
}

static const struct file_operations cmdline_proc_fops = {
	.open		= cmdline_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void patch_flag_set_val(char *cmd, const char *flag, const char *val)
{
	size_t flag_len, val_len;
	char *start, *end;

	start = strstr(cmd, flag);
	if (!start)
		return;

	flag_len = strlen(flag);
	val_len = strlen(val);
	end = start + flag_len + strcspn(start + flag_len, " ");
	memmove(start + flag_len + val_len, end, strlen(end) + 1);
	memcpy(start + flag_len, val, val_len);
}

static void patch_flag_remove_flag(char *cmd, const char *flag)
{
	char *offset_addr = cmd;
	offset_addr = strstr(cmd, flag);
	if (offset_addr) {
		size_t i, len, offset;

		len = strlen(cmd);
		offset = offset_addr - cmd;

		for (i = 1; i < (len - offset); i++) {
			if (cmd[offset + i] == ' ')
				break;
		}

		memmove(offset_addr, &cmd[offset + i + 1], len - i - offset);
	} else {
		printk("%s: Unable to find flag \"%s\"\n", __func__, flag);
	}
}

static void patch_safetynet_flags(char *cmd)
{
	patch_flag_set_val(cmd, "androidboot.flash.locked=", "1");
	patch_flag_set_val(cmd, "androidboot.verifiedbootstate=", "green");
	patch_flag_set_val(cmd, "androidboot.veritymode=", "enforcing");
	patch_flag_set_val(cmd, "androidboot.vbmeta.device_state=", "locked");
}

static void patch_sar_flags(char *cmd)
{
	patch_flag_remove_flag(cmd, "root=PARTUUID=");
	patch_flag_remove_flag(cmd, "rootwait");
	patch_flag_remove_flag(cmd, "androidboot.realmebootstate=");
	/* This flag is skip_initramfs, Omit the last 2 characters to avoid getting patched by Magisk */
	patch_flag_remove_flag(cmd, "skip_initram");
}

static bool in_recovery;

static int __init boot_mode_setup(char *value)
{
	in_recovery = !strcmp(value, "recovery");
	return 1;
}
__setup("androidboot.mode=", boot_mode_setup);

static int __init proc_cmdline_init(void)
{
#ifdef CONFIG_INITRAMFS_IGNORE_SKIP_FLAG
	proc_command_line_init();
#endif

	strcpy(new_command_line, saved_command_line);

	/*
	 * Patch various flags from command line seen by userspace in order to
	 * pass SafetyNet checks.
	 */
	if (!in_recovery)
		patch_safetynet_flags(new_command_line);

	if (check_rdp())
		patch_sar_flags(new_command_line);

	proc_create("cmdline", 0, NULL, &cmdline_proc_fops);
	return 0;
}
fs_initcall(proc_cmdline_init);
