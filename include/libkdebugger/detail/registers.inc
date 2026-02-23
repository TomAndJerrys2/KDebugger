#ifndef DEFINE_REGISTER
	#error "This file is intended for textual inclusion with the DEFINE_REGISTER macro defined"
#endif

// gets the offset of a register member and adds it to
// the offset of a given register, defined by the user_regs_struct
#define GPR_OFFSET(reg) (offsetof(user, regs) + offsetof(user_regs_struct, reg))

// helper defines a 64bit GPR
#define DEFINE_GPR_64(name, dwarf_id) \
	DEFINE_REGISTER(name, dwarf_id, 8, GPR_OFFSET(name), \
		register_type::gpr, register_format::uint)

// helper macro for 32bit sub GPR
#define DEFINE_GPR_32(name, super) \
	DEFINE_REGISTER(name, -1, 4, GPR_OFFSET(super), \
		register_type::sub_gpr, register_format::uint)

// helper macro for 16bit sub GPR
#define DEFINE_GPR_16(name, super) \
	DEFINE_REGISTER(name, -1, 2, GPR_OFFSET(super), \
		register_type::sub_gpr, register_format::uint)

// helper macro for 8bit HIGH sub GPR
#define DEFINE_GPR_8H(name, super) \
	DEFINE_REGISTER(name, -1, 1, GPR_OFFSET(super) + 1, \
		register_type::sub_gpr, register_format::uint)

// helper macro for 8bit LOW sub GPR
#define DEFINE_GPR_8L(name, super) \
	DEFINE_REGISTER(name, -1, 1, GPR_OFFSET(super), \
		register_type::sub_gpr, register_format::uint)


