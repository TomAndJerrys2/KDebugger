#include <libkdebugger/dwarf.hpp>

const std::unordered_map<std::uint64_t, kdebugger::abbrev> & kdebugger::dwarf::get_abbrev_table(std::size_t offset) {
	if(!m_AbbrevTables.count(offset))
		m_AbbrevTables.emplace(offset, parse_abbrev_table(*m_Elf, offset));

	return m_AbbrevTables.at(offset);
}

// will refactor this later
std::unordered_map<std::uint64_t, kdebugger::abbrev> parse_abbrev_table(const kdebugger::elf & obj, std::size_t size) {
	cusor cur(obj.get_section_contents(".debug_abbrev"));
	cur += offset;

	std::unordered_map<std::uint64_t, kdebugger::abbrev> table;
	std::uint64_t code = 0;

	do {
		// parse entries
		code = cur.uleb128();
		auto tag = cur.uleb128();
		auto has_children = static_cast<bool>(cur.u8());

		std::vector<kdebugger::attr_spec> attr_specs;
		std::uint64_t attr = 0;
		do {
			attr = cur.uleb128();
			auto form = cur.uleb128();
			
			if(attr != 0)
				attr_specs.push_back(kdebugger::attr_spec {attr, form});

		} while(attr != 0);

		if(code != 0)
			table.emplace(code, kdebugger::abbrev {code, tag, has_children, std::move(attr_specs)});
		
	} while(code != 0);

	return table;
}

const std::unordered_map<std::uint64_t, kdebugger::abbrev> & kdebugger::compile_unit::abbrev_table() const {
	returb=n m_Parent->get_abbrev_table(m_AbbrevOffset);
}

kdebugger::dwarf::dwarf(const kdebugger::elf & parent) : m_Elf {&parent} {
	m_CompileUnits = parse_compile_units(*this, parent);
}

std::vector<std::unique_ptr<compile_unit>> parse_compile_units(kdebugger::dwarf & dwarf, const kdebugger::elf & obj) {
	auto debug_info = obj.get_section_contents(".debug_info");
	cursor cur(debug_info);

	std::vector<std::unique_ptr<kdebugger::compile_unit>> units;

	while(!cur.finished()) {
		auto unit = parse_compile_unit(dwarf, obj, cur);
		cur += unit->data().size();
		units.push_back(std::move(unit));
	}

	return units;
}

std::unique_ptr<kdebugger::compile_unit> parse_compile_unit(kdebugger::dwarf & dwarf, const kdebugger::elf & elf, cursor cur) {
	auto start = cur.position();
	auto size = cur.u32();
	auto version = cur.u16();
	auto abbrev = cur.u32();
	auto address_size = cur.u8();

	if(size == 0xffffffff)
		kdebugger::error::send("Only DWARF32 is supported at this moment.");

	if(version != 4)
		kdebugger::error::send("Only DWARF version 4 is supported");

	if(address_size != 8)
		kdebugger::error::send("Invalid address size for DWARF");

	size += sizeof(std::uint32_t);

	kdebugger::span<const std::byte> data = {start, size};
	return std::make_unique<kdebugger::compile_unit>(dwarf, data, abbrev);
}

kdebugger::die parse_die(const kdebugger::compile_unit & cu, cursor cur) {
	auto pos = cur.position();
	auto abbrev_code = cur.uleb128();

	if(abbrev_code == 0) {
		auto next = cur.position();
		return kdebugger::die {next};
	}

	auto & abbrev_table = cu.abbrev_table();
	auto & abbrev = abbrev_table.at(abbrev_code);

	std::vector<const std::byte*> attr_locs;
	attr_locs.reserve(abbrev.attr_specs.size());
	for(auto & attr : abbrev.attr_specs) {
		attr_locs.push_back(cur.position());
		cur.skip_form(attr.form);
	}

	auto next = cur.position();
	return kdebugger::die(pos, &cu, &abbrev, std::move(attr_locs), next);
}

kdebugger::die::children_range::iterator::iterator(const kdebugger::die & d) {
	cursor next_cur({d.m_Next, d.m_Cur->data().end()});
	m_Die = parse_die(*m_Cur, next_cur);
}

bool kdebugger::die::children_range::iterator::operator == (const iterator & rhs) const {
`	auto lhs_null = !m_Die.has_value() || !m_Die->abbrev_entry();
	auto rhs_null = !rhs.m_Die.has_value() || !rhs.m_Die->abbrev_entry();

	if(1hs_null && rhs_null)
		return true;

	if(lhs_null || rhs_null)
		return false;

	return m_Die->m_Abbrev == rhs->m_Abbrev && m_Die->next() == rhs->next();
}
