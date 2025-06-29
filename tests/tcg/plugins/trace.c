#include <qemu-plugin.h>
#include <glib.h>
#include <stdio.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static struct qemu_plugin_register *reg_fp;
static struct qemu_plugin_register *reg_lr;
static GByteArray *buf;
uint64_t last_fp;

static uint64_t read_register(struct qemu_plugin_register *reg)
{
    g_byte_array_set_size(buf, 0);
    size_t sz = qemu_plugin_read_register(reg, buf);
    g_assert(sz == 8);
    g_assert(buf->len == 8);
    uint64_t val = *((uint64_t *) buf->data);
    return val;
}

static uint64_t read_memory(uint64_t addr)
{
    g_byte_array_set_size(buf, 0);
    bool read = qemu_plugin_read_memory_vaddr(addr, buf, 8);
    if (!read) {
        return 0;
    }
    g_assert(read);
    return *((uint64_t *) buf->data);

}

static void insn_exec(unsigned int cpu_index, void *udata)
{
    uint64_t pc = (uintptr_t) udata;
    uint64_t fp = read_register(reg_fp);
    if (fp != 0 && fp == last_fp) {
        return;
    }
    last_fp = fp;
    g_autoptr(GString) out = g_string_new("");
    g_string_append_printf(out, "%"PRIx64, pc);
    uint64_t lr = read_register(reg_lr);
    if (lr) {
        g_string_append_printf(out, " %"PRIx64, lr);
    }
    while (fp != 0) {
        uint64_t return_addr = read_memory(fp + 8);
        g_string_append_printf(out, " %"PRIx64, return_addr);
        fp = read_memory(fp);
    }
    g_string_append(out, "\n");
    qemu_plugin_outs(out->str);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    for (int idx = 0; idx < qemu_plugin_tb_n_insns(tb); ++idx) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, idx);
        uintptr_t pc = qemu_plugin_insn_vaddr(insn);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, insn_exec, QEMU_PLUGIN_CB_R_REGS,
                                         (void *) pc);
    }
}

static void init_registers(void)
{
    g_autoptr(GArray) regs = qemu_plugin_get_registers();
    for (int i = 0; i < regs->len; ++i) {
        qemu_plugin_reg_descriptor *reg;
        reg = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
        if (!strcmp(reg->name, "x29")) {
            reg_fp = reg->handle;
        } else if (!strcmp(reg->name, "x30")) {
            reg_lr = reg->handle;
        }
    }
    g_assert(reg_fp);
    g_assert(reg_lr);
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    init_registers();
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    buf = g_byte_array_new();
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    return 0;
}
