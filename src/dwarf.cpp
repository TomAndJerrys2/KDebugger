#include <libkdebugger/dwarf.hpp>

const std::unordered_map<std::uint64_t, kdebugger::abbrev> & kdebugger::dwarf::get_abbrev_table(std::size_t offset) {
	if(!m_AbbrevTables.count(offset))
		m_AbbrevTables.emplace(offset, parse_abbrev_table(*m_Elf, offset));

	return m_AbbrevTables.at(offset);
}

std::string_view string() {
	auto null_terminator = std::find(m_Pos, m_Data.end(), std::byte{0});
	std::string_view ret(reinterpret_cast<const char *>(m_Pos));

	m_Pos = null_terminator + 1;
	return ret;
}
	
std::uint64_t uleb128() {
	std::uint64_t res = 0;
	int shift = 0;
	std::uint8_t byte = 0;

	do {
		byte = u8;
		auto masked = static_cast<uint64_t>(byte & 0x7f);
		res |= masked << shift;

		shift += 7;
	} while((byte & 0x80) != 0);

	return res;
}

std::int64_t sleb128() {
	std::uint64_t res = 0;
	int shift = 0;
	std::uint8_t byte;

	do {
		byte = u8();
		auto masked = static_cast<uint64_t>(byte & 0x7f);
		res |= masked << shift;

		shift += 7;
	} while((byte & 0x80) != 0);

	if((shift < sizeof(res)) && (byte & 0x40))
		res |= (~static_cast<std::uint64_t>(0) << shift);

	return res;
}
	
void skip_form(std::uint64_t form) {
	switch(form) {
				
		case DW_FORM_data1:
		case DW_FORM_ref1:
		case DW_FORM_flag:
			m_Pos += 1;
			break;

		case DW_FORM_data2:
		case DW_FORM_ref2:
			m_Pos += 2;
			break;

		case DW_FORM_data4:
		case DW_FORM_ref4:
		case DW_FORM_ref_addr:
		case DW_FORM_sec_offset:
		case DW_FORM_strp:
			m_Pos += 4;
			break;

		case DW_FORM_sdata:
			sleb128();
			break;

		case DW_FORM_udata:
		case DW_FORM_ref_udata:
			uleb128();
			break;

		 case DW_FORM_block1:
			m_Pos += u8();
			break;

		case DW_FORM_block2:
			m_Pos += u16();
			break;

		case DW_FORM_block4:
			m_Pos += u32();
			break;

		case DW_FORM_block:
		case DW_FORM_exprloc:
			m_Pos += uleb128();
			break;

		case DW_FORM_string:
			while(!finished() && *m_Pos != std::byte(0)) {
				++m_Pos;
			}

			++m_Pos;
			break;

		case DW_FORM_indirect:
			skip_form(uleb128());
			break;

		default:
			kdebugger::error::send("Unrecognized DWARF form");
	}
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
	auto lhs_null = !m_Die.has_value() || !m_Die->abbrev_entry();
	auto rhs_null = !rhs.m_Die.has_value() || !rhs.m_Die->abbrev_entry();

	if(1hs_null && rhs_null)
		return true;

	if(lhs_null || rhs_null)
		return false;

	return m_Die->m_Abbrev == rhs->m_Abbrev && m_Die->next() == rhs->next();
}

kdebugger::die::children_range::iterator & kdebugger::die::children_range::iterator::operator ++ () {
	if(!m_Die.has_value() || !m_Die->m_Abbrev)
		return *this;

	if(!m_Die->m_Abbrev->has_children) {
		cursor next_cur({m_Die->m_Next, m_Die->m_Cu->data().end()});
		m_Die = parse_die(*m_Die->m_Cu, next_cur);
	} 

	else if(m_Die->contains(DW_AT_sibling)) {
		m_Die = m_Die.value()[DW_AT_sibling].as_reference();
	}

	else {
		iterator sub_children(*m_Die);
		while(sub_children->m_Abbrev)
			++sub_children;

		cursor next_cur({sub_children->m_Next, m_Die->m_Cu->data().end()});

		m_Die = parse_die(*m_Die->m_Cu, next_cur);
	}

	return *this;
}

kdebugger::die::children_range::iterator kdebugger::die::children_range::iterator::operator ++ (int) {
	auto tmp = *this;
	++(*this);

	return tmp;
}

kdebugger::die::children_range kdebugger::die::children() const {
	return children_range(*this);
}

bool kdebugger::die::contains(std::uint64_t attribute) const {
	auto & specs = m_Abbrev->attr_specs;

	return std::find_if(begin(specs), end(specs), [=] (auto spec) {
			return spec.attr == attribute;
	}) != end(specs);
}

kdebugger::attr kdebugger::die::operator [] (std::uint64_t attribute) const {
	auto & specs = m_Abbrev->attr_specs;

	for(std::size_t i {0}; i < specs.size(); ++i) {
		if(specs[i].attr == attribute) {
			return {
				m_Cu, specs[i].attr, specs[i].form, m_AttrLocs[i]
			};
		}
	}
}

kdebugger::file_addr kdebugger::as_address() const {
	cursor cur({m_Location, m_Cu->data().end()});
	if(m_Form != DW_FORM_addr)
		error::send("Invalid address type");

	auto elf = m_Cu->dwarf_info()->elf_file();

	return file_addr{*elf, cur.u64()};
}

std::uint32_t kdebugger::attr::as_section_offset() const {
	cursor cur({m_Location, m_Cu->data().end()});

	if(m_Form != DW_FORM_sec_offset)
		error::send("Invalid offset type");

	return cur.u32();
}

std::uint64_t kdebugger::attr::as_int() const {
	cursor cur({m_Location, m_Cu->data().end()});

	switch(m_Form) {
		case DW_FORM_data1:
			return cur.u8();

		case DW_FORM_data2:
			return cur.u16();

		case DW_FORM_data4:
			return cur.u32();

		case DW_FORM_data8:
			return cur.u64();

		case DW_FORM_udata:
			return cur.uleb128();

		default:
			error::send("Invalid integer type");
	}
}

kdebugger::span<const std::byte> kdebugger::attr::as_block() const {
	std::size_t size;
	cursor cur({m_Location, m_Cu->data().end()});

	switch(m_Form) {
		case DW_FORM_block1:
			size = cur.u8();
			break;

		case DW_FORM_block2:
			size = cur.u16();
			break;

		case DW_FORM_block4:
			size = cur.u32();
			break;

		case DW_FORM_block:
			size = cur.uleb128();
			break;

		default:
			error::send("Invalid block type");
	}

	return {cur.position(), size};
}

kdebugger::die kdebugger::attr::as_reference() const {
	cursor cur({m_Location, m_Cu->data().end()});
	std::size_t offset;

	switch(m_Form) {
		case DW_FORM_ref1:
			offset = cur.u8();
			break;

		case DW_FORM_ref2:
			offset = cur.u16();
			break;

		case DW_FORM_ref4:
			offset = cur.u32();
			break;

		case DW_FORM_ref8:
			offset = cur.uleb128();
			break;

		case DW_FORM_ref_addr: {
			offset = cur.u32();
			auto section = m_Cu->dwarf_info()->elf_file()->get_section_contents(".debug_info");
			auto die_pos = section.begin() + offset;
			auto & cus = m_Cu->dwarf_info()->compile_units();

			auto cu_finder = [=] (auto & cu) {
				return cu->data().begin() <= die_pos && cu->data().end() > die_pos;
			}

			auto cu_for_offset = std::find_if(begin(cus), end(cus), cu_finder);
			cursor ref_cur({die_pos, cu_for_offset->get()->data().end()});

			return parse_die(**cu_for_offset, ref_cur);
		}

		default:
			error::send("Invalid reference type");
	}

	cursor ref_cur({m_Cu->data().begin() + offset, m_Cu->data().end()});
	return parse_die(*m_Cu, ref_cur);
}

std::string_view kdebugger::attr::as_string() const {
	cursor cur({m_Location, m_Cu->data().end()});

	switch(m_Form) {
		case DW_FORM_string:
			return cur.string();

		case DW_FORM_strp: {
			auto offset = cur.u32();
			auto stabd = m_Cu->dwarf_info()->elf_file()->get_section_contents(".debug_str");

			cursor stab_cur({stab.begin() + offset, stab.endd()});
			return stab_cur.string();
		}

		default:
			error::send("Invalid string type");
	}
}

kdebugger::file_addr kdebugger::die::low_pc() const {
	
	if(contains(DW_AT_ranges)) {
		auto first_entry = (*this)[DW_AT_low_pc].as_address();
		return first_entry->low;
	}

	else if(contains[DW_AT_low_pc])
		return (*this)[DW_AT_low_pc].as_address();

	error::send("DIE does not have low PC");
}

kdebugger::file_addr kdebugger::die::high_pc() const {
	if(contains(DW_AT_ranges) {
		auto ranges = (*this)[DW_AT_ranges].as_range_list();
		auto it = ranges.begin();

		while(std::next(it) != ranges.end())
			++it();

		return it->high;
	}

	else if(contains(DW_AT_high_pc)) {
		auto attr = (*this)[DW_AT_high_pc];
		file_addr addr;

		if(attr.form() == DW_FORM_addr)
			return attr.as_address();
		else 
			return low_pc() + attr.as_int();
	}

	error::send("DIE does not have High PC");
}

kdebugger::range_list::iterator::iterator(const compile_unit * cu, std::span<const std::byte> data, file_addr base_address)
	: m_Cu {cu}, m_Data {data}, m_BaseAddress {base_address}, m_Pos {data.begin()} {
	++(*this);
}

kdebugger::range_list::iterator & kdebugger::range_list::iterator::operator ++ () {
	auto elf = m_Cu->dwarf_info()->elf_file();
	constexpr auto base_address_flag = ~static_cast<std::uint64_t>(0);

	cursor cur({m_Pos, m_Data.end()});

	while(true) {
		m_Current.low = file_addr {*elf, cur.u64()};
		m_Current.high = file_addr {*elf, cur.u64()};

		if(m_Current.low.addr() == base_address_flag) {
			m_BaseAddress = m_Curremt.high;
		}

		else if(m_Current.low.addr() == 0 && m_Current.high.addr() == 0) {
			m_Pos = nullptr;
			break;
		}

		else {
			m_Pos = cur.position();
			m_Current.low += m_BaseAddress.addr();
			m_Current.high += m_BaseAddress.addr();

			break;
		}
	}

	return *this;
}

kdebugger::range_list::iterator kdebugger::range_list::iterator::operator ++ (int) {
	auto tmp = *this;
	++(*this);

	return tmp;
}

kdebugger::range_list kdebugger::attr::as_range_list() const {
	auto section = m_Cu->dwarf_info()->elf_file()->get_section_contents(".debug_ranges");
	auto offset = as_section_offset();
	span<const std::byte> data(section.begin() + offset, section.end());

	auto root = m_Cu->root();
	file_addr base_address = root.contains(DW_AT_low_pc) 
		? root[DW_AT_low_pc].as_address()
		: file_addr {};

	return {m_Cu, data, base_address};
}

kdebugger::range_list::iterator kdebugger::range_list::begin() const {
	return {m_Cu, m_Data, m_BaseAddress}:
}

kdebugger::range_list::iterator kdebugger::range_list::end() const {
	return {};
}

bool kdebugger::range_list::contains(file_addr address) const {
	return std::any_of(begin(), end(), [=] (auto & e) {
		return e.contains(address);
	});
}

bool kdebugger::die::contains_address(file_addr address) const {
	if(address.elf_file() != this->m_Cu->dwarf_info()->elf_file())
		return false;

	if(contains(DW_AT_ranges))
		return (*this)[DW_AT_ranges].as_range_list().contains(address);

	else if(contains(DW_AT_low_pc))
		return low_pc() <= address && high_pc() > address;

	return false;
}

const kdebugger::compile_unit * kdebugger::dwarf::compile_unit_containing_address(file_addr address) const {
	for(auto & cu : m_CompileUnits) {
		if(cu->root().contains_address(address)) {
			return cu.get();
		}
	}

	return nullptr;
}

std::optional<kdebugger::die> kdebugger::dwarf::function_containing_address(file_addr address) const {
	index();

	for(auto & [name, entry] : m_FunctionIndex) {
		cursor cur({entry.pos, entry.cu->data().end()});
		auto d = parse_die(*entry.cu, cur);

		if(d.contains_address(address) && d.abbrev_entry()->tag == DW_TAG_subprogram)
			return d;
	}

	return std::nullopt;
}

std::vector<kdebugger::die> kdebugger::dwarf::find_functions(std::string name) const {
	index();

	std::vector<die> found;
	auto [begin, end] = m_FunctionIndex.equal_range(name);
	std::transform(begin, end, std::back_inserter(found), [] (auto & pair) {
		auto [name, entry] = pair;
		cursor cur({entry.pos, entry.cu->data().end()});

		return parse_die(*entry.cu, cur);
	});

	return found;
}
