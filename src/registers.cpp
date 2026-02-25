// generic headers
#include <iostream>

// Project specific headers
#include <libkdebugger/registers.hpp>
#include <libkdebugger/bit.hpp>
#include <libkdebugger/process.hpp>

// reading a specific valued register
kdebugger::registers::value kdebugger::registers::read(const register_info & info) const {
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
void sdb::registers::write(const register_info & info, value val) {
	auto bytes = as_bytes(m_Data);

	// visit -> takes a callback && std::variant to call a function
	// with the value stored in our type-safe union
	std::visit([&](auto & v) {
		if(sizeof(v) == info.size) {
			auto val_bytes = as_bytes(v);

			std::copy(val_bytes, val_bytes + sizeof(v),
					bytes + info.offset);
		}	

		else {
			std::cerr << "kdebugger::register::write called w/ "
				"mismatched register value and sizes";
			std::terminate();
		}
	}, val);
}
