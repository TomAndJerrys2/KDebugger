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

void kdebugger::dwarf::index() const {
	if(!m_FunctionIndex.empty())
		return;

	for(auto & cu : m_CompileUnits) {
		index_die(cu->root());
	}
}

std::optional<std::string_view> kdebugger::die::name() const {
	if(contains(DW_AT_name))
		return (*this)[DW_AT_name].as_string();

	if(contains(DW_AT_specification))
		return (*this)[DW_AT_specification].as_reference().name();

	if(contains(DW_AT_abstract_origin))
		return (*this)[DW_AT_abstract_origin].as_reference().name();

	return std::nullopt;
}

void kdebugger::dwarf::index_die(const die & current) const {
	bool has_range = current.contains(DW_AT_pc) || current.contains(DW_AT_ranges);
	auto is_function = current.abbrev_entry()->tag == DW_TAG_subprogram || 
		current.abbrev_entry()->tag == DW_TAG_subprogram || current.abbrev_entry()->tag == DW_TAG_inclined_subroutine;

	if(has_range && is_function) {
		index_entry entry {current.cu(), current.position()};
		m_FunctionIndex.emplace(*name, entry);
	}

	for(auto child : current.children()) {
		index_die(child);
	}
}

kdebugger::compile_unit::compile_unit(dwarf & parent, span<const std::byte> data, std::size_t abbrev_offset)
	: m_Parent {&parent}, m_Data {data}, m_AbbrevOffset {abbrev_offset} {
	m_LineTable = parse_line_table(*this);
}

// line table parser
namespace {
	kdebugger::line_table::file parse_line_table_file(cursor & cur, std::filesystem::path compilation_dir, 
			const std::vector<std::filesystem::path> & include_directories) {
		auto file = cur.string();
		auto dir_index = cur.uleb128();
		auto modication_time = cur.uleb128();
		auto file_length = cur.uleb128();

		std::filesystem::path path = file;
		if(file[0] != '/') {
			if(dir_index == 0)
				path = compilation_dir / std::string(file);
			else
				path = include_directories[dir_index - 1] / std::string(file);
		}

		return {path.string(), modification_time, file_length};
	}

	std::unique_ptr<kdebugger::line_table> parse_line_table(const kdebugger::compile_unit & cu) {
		auto section = cu.dwarf_info()->elf_file()->get_section_contents(".debug_line");
		if(!cu.root().contains(DW_AT_stmt_list))
			return nullptr;

		auto offset = cu.root()[DW_AT_stmt_list].as_section_offset();
		cursor cur({section.begin() + offset, section.end()});

		auto size = cur.u32();
		auto end = cur.position() + size;
		auto version = cur.u16();

		if(version != 4)
			kdebugger::error::send("Only DWARF 4 is supported right now");

		// header length
		(void)cur.u32();

		auto minimum_instruction_length = cur.u8();
		if(minimum_instruction_length != 1)
			kdebugger::error::send("Invalid minimum instruction length");

		auto maximum_operations_per_instruction = cur.u8();
		if(maximum_operations_per_instruction != 1)
			kdebugger::send("Invalid maximum operations per instruction");

		auto default_is_stmt = cur.u8();
		auto line_base = cur.u8();
		auto line_range = cur.u8();
		auto opcode_base = cur.u8();

		std::array<std::uint8_t, 12> expected_opcode_length {
			0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1
		};

		for(auto i {0}; i < opcode_base - 1; ++i) {
			if(cur.u8() != expected_opcode_lengths[i])
				kdebugger::error::send("Unexpected opcode lengths");
		}

		std::vector<std::filesystem::path> include_directories;
		std::filesystem::path compilation_dir (cu.root()[DW_AT_comp_dir].as_string());
		for(auto dir = cur.string(); !dir.empty(); dir = cur.string()) {
			if(dir[0] == '/')
				include_directories.push_back(std::string(dir));
			else
				include_directories.push_back(compilation_dir / std::string(dir));
		}
	
		std::vector<kdebugger::line_table::file> file_names;
		while(*cur.position() != std::byte(0)) {
			file_names.push_back(
				parse_line_table_file(cur, compilation_dir, include_directories)
			);
		}

		cur += 1;

		std::span<const std::byte> data {cur.position(), end};
		return std::make_unique<kdebugger::line_table> (data, &cu, 
				default_is_stmt, line_base, line_range, opcode_base, 
					std::move(include_directories), std::move(file_names)
		);
	}
}

kdebugger::line_table::iterator::iterator(const kdebugger::line_table * table) 
	: m_Table {table}, m_Pos {table->m_Data.begin()} {
	m_Registers.is_stmt = table->m_DefaultIsStmt;
	++(*this);
}

kdebugger::line_table::iterator kdebugger::line_table::begin() const {
	return iterator(this);
}

kdebugger::line_table::iterator kdebugger::line_table::end() const {
	return {};
}

kdebugger::line_table::iterator & kdebugger::line_table::iterator::operator ++ () {
	if(m_Pos == m_Table->m_Data.end()) {
		m_Pos = nullptr;
		return *this;
	}

	bool emitted = false;
	do emitted = execute_instruction();
	while(!emitted);

	m_Current.file_entry = &m_Table->m_FileNames[m_Current.file_index - 1];
	return *this;
}

kdebugger::line_table::iterator kdebugger::line_table::iterator::operator ++ (int) {
	auto tmp = *this;
	++(*this);

	return tmp;
}

bool kdebugger::line_table::iterator::execute_instruction() {
	auto elf = m_Table->m_Cu->dwarf_info()->elf_file();
	cursor cur({m_Pos, m_Table->m_Data.end()});
	auto opcode = cur.u8();
	bool emitted = false;

	if(opcode > 0 && opcode < m_Table->m_OpcodeBase) {
		switch(opcode) {
			case DW_LNS_copy:
				m_Current = m_Registers;
				m_Registers.basic_block_start = false;
				m_Registers.prologue_end = false;
				m_Registers.epilogue_begin = false;
				m_Registers.discriminator = 0;

				emitted = true;
				break;
	
			case DW_LNS_advance_pc:
				m_Registers.address += cur.sleb128();
				break;

			case DW_LNS_set_file:
				m_Registers.column = cur.uleb128();
				break;

			case DW_LNS_negate_stmt:
				m_Registers.is_stmt = !m_Registers.is_stmt;
				break;

			case DW_LNS_set_basic_block:
				m_Registers.basic_block_start = true;
				break;

			case DW_LNS_const_add_pc:
				m_Registers.address += (255 - m_Table->m_OpcodeBase) / m_Table->m_LineRange;
				break;

			case DW_LNS_fixed_advance_pc:
				m_Registers.address += cur.u16();
				break;

			case DW_LNS_set_prologue_end:
				m_Registers.prologue_end = true;
				break;

			case DW_LNS_set_epilogue_begin:
				m_Registers.epilogue_begin = true;
				break;

			case DW_LNS_set_isa:
				break;

			default:
				error::send("Unexpected standard opcode");
		}
	}

	else if(opcode == 0) {
		auto length = cur.uleb128();
		auto extended_opcode = cur.u8();

		switch(extended_opcode) {
			case DW_LNE_end_sequence:
				m_Registers.end_sequence = true;
				m_Current = m_Registers;
				m_Registers = entry {};
				m_Registers.is_stmt = m_Table->m_DefaultIsStmt;
				emitted = true;
				break;

			case DW_LINE_set_address:
				m_Registers.address = file_addr(*elf, cur.u64());
				break;

			case DW_LNE_define_file: {
				auto compilation_dir = m_Table->m_Cu->root()[DW_AT_comp_dir].as_string();
				auto file = parse_line_table_file(cur, std::string(compilation_dir), m_Table->m_IncludeDirectories);
				m_Table->m_FileNames.push_back(file);
				break;

			case DW_LNE_set_discriminator:
				m_Registers.discriminator = cur.uleb128();
				break;

			default:
				error::send("Unexpected extended opcode");
			}
		}
	}

	else {
		auto adjusted_opcode = opcode - m_Table->m_OpcodeBase;
		m_Registers.address += adjusted_opcode / m_Table->m_LineRange;
		m_Registers.line += m_Table->m_LineBase + (adjusted_opcode % m_Table->m_LineRange);
		m_Current = m_Registers;
		m_Registers.basic_block_start = false;
		m_Registers.prologue_end = false;
		m_Registers.epilogue_begin = false;
		m_Registers.discriminator = 0;
		emitted = true;
	}

	m_Pos = cur.position();
	return emitted;
}

kdebugger::line_table::iterator kdebugger::line_table::get_entry_by_address(file_addr address) const {
	auto prev = begin();
	if(prev == end())
		return prev;

	auto it = prev;
	for(++it; it != end(); prev = it++) {
		if(prev->address <= address && it->address > address && !prev->end_sequence) {
			return prev;
		}
	}

	return end();
}

bool path_ends_in(const std::filesystem::path & lhs, const std::filesystem::path & rhs) {
	auto lhs_size = std::distance(lhs.begin(), lhs.end());
	auto rhs_size = std::distance(rhs.begin(), rhs.end());

	if(rhs_size > lhs_size)
		return false;

	auto start = std::next(lhs.begin(), lhs_size - rhs_size);
	return std::equal(start, lhs.end(), rhs.begin());
}

std::vector<kdebugger::line_table::iterator> kdebugger::line_table::get_entries_by_line(std::filesystem::path path, std::size_t size) const {
	std::vector<iterator> entries;

	for(auto it = begin(); it != end(); ++it) {
		auto & entry_path = it->file_entry->path;
		
		if((path.is_absolute() && entry_path == path) || (path.is_relative() && path_ends_in(entry_path, path)))
			entries.push_back(it);
	}

	return entries;
}

kdebugger::source_location kdebugger::die::location() const {
	return {&file(), line()};
}

const kdebugger::line_table::file & kdebugger::die::file() const {
	std::uint64_t idx;
	if(m_Abbrev->tag == DW_TAG_inlined_subroutine) {
		idx = (*this)[DW_AT_call_file].as_int();
	}

	else {
		idx = (*this)[DW_AT_decl_file].as_int();
	}

	return this->m_Cu->lines().file_names[idx - 1];
}

std::uint64_t kdebugger::die::line() {
	if(m_Abbrev->tag == DW_TAG_inlined_subroutine)
		return (*this)[DW_AT_call_line].as_int();

	return (*this)[DW_AT_decl_line].as_int();
}

std::vector<kdebugger::die> kdebugger::dwarf::inline_stack_at_address(file_addr address) const {
	auto func = function_containing_address(address);
	std::vector<kdebugger::die> stack;

	if(func) {
		stack.push_back(*func);

		while(true) {
			const auto & children = stack.back().children();
			auto found = std::find_if(children.begin(), children.end(), [=] {
				return (child.abbrev_entry()->tag == DW_TAG_inline_subroutine 
					&& child.contains_address(address));
			});

			if(found == children.end())
				break;
			else
				stack.push_back(*found);

		}
	}

	return stack;
}

const kdebugger::call_frame_information::common_information_entry & kdebugger::call_frame_information::get_cie(file_offset at) const {
	auto offset = at.off();
	if(m_CieMap.count(offset))
		return m_CieMap.at(offset);

	auto section = at.elf_file()->get_section_contents(".eh_frame");
	cursor cur({at.elf_file()->file_offset_as_data_pointer(at), section.end()});
	auto cie = parse_cie(cur);
	m_CieMap.emplace(offset, cie);
	return m_CieMap.at(offset);
}

std::uint64_t parse_eh_frame_pointer_with_base(cursor & cur, std::uint8_t encoding, std::uint64_t base) {
	switch(encoding & 0x0f) {
		case DW_EH_PE_absptr:
			return base + cur.u64();

		case DW_EH_PE_uleb128:
			return base + cur.uleb128();

		case DW_EH_PE_udata2:
			return base + cur.u16();

		case DW_EH_PE_udata4:
			return base + cur.u32();

		case DW_EH_PE_udata8:
			return base + cur.u64();

		case DW_EH_PE_sleb128:
			return base + cur.sleb128();

		case DW_EH_PE_sdata2:
			return base + cur.s16();

		case DW_EH_PE_sdata4:
			return base + cur.s32();

		case DW_EH_PE_sdata8:
			return base + cur.s64();

		default:
			kdebugger::error::send("Unkown eh_frame pointer encoding");
	}
}

kdebugger::call_frame_information::common_information_entry parse_cie(cursor cur) {
	auto start = cur.position();
	auto length = cur.u32() + 4;
	auto id = cur.u32();
	auto version = cur.u8();

	if(!(version == 1 || version == 3 || version == 4))
		kdebugger::error::send("Invalid CIE version");

	auto augmentation = cur.string();
	if(!augmentation.empty() && augmentation[0] != 'z')
		kdebugger::error::send("Invalid CIE augmentation");

	if(version == 4) {
		auto address_size = cur.u8();
		auto segment_size = cur.u8();
	
		if(address_size != 8)
			kdebugger::error::send("Invalid address size");

		if(segment_size != 0)
			kdebugger::error::send("Invalid segment size");
	}

	auto code_alignment_factor = cur.uleb128();
	auto data_alignment_factor = cur.sleb128();
	auto return_address_register = version == 1 ? cur.u8() : cur.uleb128();

	std::uint8_t fde_pointer_encoding = DW_EH_PE_udata8 | DW_EH_PE_absptr;
	for(auto c : augmentation) {
		switch(c) {
			case 'z':
				cur.uleb128();
				break;

			case 'R':
				fde_pointer_encoding = cur.u8();
				break;

			case 'L':
				cur.u8();
				break;

			case 'P':
				auto encoding = cur.u8();
				(void)parse_eh_frame_pointer_with_base(cur, encoding, 0);
				break;

			default:
				kdebugger::error::send("Invalid CIE augmentation");
		}
	}

	kdebugger::span<const std::byte> instructions = {
		cur.position(), start + length
	};

	bool fde_has_augmentation = !augmentation.empty();
	return { length, code_alignment_factor,
		data_alignment_factor, fde_has_augmentation,
		fde_pointer_encoding, instructions };;
}

std::uint64_t parse_eh_frame_pointer(const kdebugger::elf & elf, cursor & cur, std::uint8_t encoding, std::uint64_t pc, std::uint64_t text_section_start, 
		std::uint64_t data_section_start, std::uint64_t func_start) {
	std::uint64_t base = 0;

	switch(encoding & 0x70) {
		case DW_EH_PE_absptr:
			break;

		case DW_EH_PE_pcrel:
			base = pc;
			break;

		case DW_EH_PE_textrel:
			base = text_section_start;
			break;

		case DW_EH_PE_datarel:
			base = data_section_start;
			break;

		case DW_EH_PE_funcrel:
			base = func_start;
			break;

		default:
			kdebugger::error::send("Unkown eh_frame pointer encoding");
	}

	return parse_eh_frame_pointer_with_base(cur, encoding, base);
}

kdebugger::call_frame_information::frame_description_entry parse_fde(const kdebugger::call_frame_information & cfi, cursor cur) {
	auto start = cur.position();
	auto length = cur.u32() + 4;
}
