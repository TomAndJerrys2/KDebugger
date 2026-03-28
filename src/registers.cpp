// generic headers
#include <iostream>
#include <type_traits>
#include <algorithm>

// Project specific headers
#include <libkdebugger/registers.hpp>
#include <libkdebugger/bit.hpp>
#include <libkdebugger/process.hpp>

namespace {
	// reading a specific valued register
	kdebugger::registers::value kdebugger::registers::read(const register_info & info) const {
		if(is_undefined(info.id))
			kdebugger::error::send("Register is undefined");

		auto bytes = as_bytes(m_Data);
		if(info.format == register_format::uint) {
			switch(info.size) {
				case 1:
					return from_bytes<std::uint8_t> (bytes + info.offset);

				case 2:
					return from_bytes<std::uint16_t> (bytes + info.offset);

				case 4:
					return from_bytes<std::uint32_t> (bytes + info.offset);

				case 8:
					return from_bytes<std::uint64_t> (bytes + info.offset);

				default:
					kdebugger::error::send("Unexpected register size");
			}
		}

		else if(info.format == register_format::double_float) {
			return from_bytes<double> (bytes + info.offset);
		}

		else if(info.format == register_format::long_double) {
			return from_bytes<long double> (bytes + info.offset);
		}

		else if(info.format == register_format::vector && info.size == 8) {
			return from_bytes<byte64> (bytes + info.offset);
		}

		else {
			return from_bytes<byte128> (bytes + info.offset);
		}
	}

	// writing to a register in bytes
	void sdb::registers::write(const register_info & info, value val, bool commit) {
		auto bytes = as_bytes(m_Data);

		// visit -> takes a callback && std::variant to call a function
		// with the value stored in our type-safe union
		std::visit([&](auto & v) -> void {
			if(sizeof(v) <= info.size) {
				auto wide = widen(info, v);

				auto val_bytes = as_bytes(wide);
				std::copy(val_bytes, val_bytes + sizeof(v),
						bytes + info.offset);
			}	

			else {
				std::cerr << "kdebugger::register::write called w/ "
					"mismatched register value and sizes";
				std::terminate();
			}
		}, val);


		if(info.type == register_type::fpr) {
			m_Process->write_fprs(m_Data.i387);
		}

		else {	
			// aligns the lowest 3 bits in the address to 0
			// forced to be 8 byte aligned 
			auto aligned_offset = info.offset & ~0b111;
			m_Process->write_user_area(aligned_offset, 
					from_bytes<std::uint64_t> (bytes + aligned_offset));
		}

		if(commit) {
			if(info.type == register_type::fpr)
				m_Proc->write_fprs(m_Data.i387);

			else {
				auto aligned_offset = info.offset & ~ 0b111;
				m_Proc->write_user_area(aligned_offset, from_bytes<std::uint64_t>(bytes + aligned_offset));
			}
		}
	}
	
	// widens a value to store in a larger register
	template <class T>
	kdebugger::byte128 widen(const kdebugger::register_info & info, T t) {
		
		using namespace kdebugger;
		if constexpr (std::is_floating_point_v<T>) {
			if(info.format == register_format::double_float)
				return to_byte128(static_cast<double> (t));

			if(info.format == register_format::long_double)
				return to_byte128(static_cast<long double> (t));
		}

		else if constexpr (std::is_signed_v<T>) {
			if(info.format == register_format::uint) {
				switch(info.size) {
					case 2:
						return to_byte128(static_cast<std::int16_t> (t));

					case 4:
						return to_byte128(static_cast<std::int32_t> (t));

					case 8:
						return to_byte128(static_cast<std::int64_t> (t));
				}
			}
		}

		return to_byte128(t);
	}
}

void kdebugger::registers::undefine(register_id id) {
	std::size_t canonical_offset = register_info_by_id(id).offset >> 1;
	m_Undefined.push_back(canonical_offset);
}

bool kdebugger::registers::is_undefined(register_id id) const {
	std::size_t canonical_offset = register_info_by_id(id).offset >> 1;
	return std::find(begin(m_Undefined), end(m_Undefined), canonical_offset)
		!= end(m_Undefined);
}


void kdebugger::registers::flush() {
	m_Proc->write_fprs(m_Data.i387);
	m_Proc->write_gprs(m_Data.regs);
	auto info = register_info_by_id(register_id::dr0);

	for(auto i {0}; i < 8; ++i) {
		if(i == 4 || i == 5)
			continue;

		auto reg_offset = info.offset + sizeof(std::uint64_t) * i;
		auto ptr = reinterpret_cast<std::byte *>(m_Data.u_debugreg + i);
		auto bytes = from_bytes<std::uint64_t>(ptr);

		m_Proc->write_user_area(reg_offset, bytes);
	}
}
