#pragma once

// general header includes
#include <vector>
#include <memory>
#include <algorithm>
#include <type_traits>

// Private / project-specific headers
#include <libkdebugger/types.hpp>
#include <libkdebugger/error.hpp>

namespace kdebugger {

	template <class Stoppoint, bool Owning = true>
	class stoppoint_collection {
		
		private:
			using points_t = std::vector<pointer_type>;

			// find by ID or address as iterator types
			typename points_t::iterator find_by_id(typename Stoppoint::id_type id);
			typename points_t::const_iterator find_by_id(typename Stoppoint::id_type id) const;

			typename points_t::iterator find_by_address(virt_addr address);
			typename points_t::const_iterator find_by_address(virt_addr address) const;		

			points_t m_Stoppoints;

		public:

			using pointer_type = std::conditional_t<Owning, std::unique_ptr<Stoppoint>,
				  Stoppoint *>;

			Stoppoint & push(pointer_type bs);

			// checks if the collection contains a stop point either
			// matching a given Id or a virtual address
			bool contains_id(typename Stoppoint::id_type id) const;
			bool contains_address(virt_addr address) const;
			bool enable_stoppoint_at_address(virt_addr address) const;
	
			// gets a stop point by its id -> overload for returning constant
			// l-value reference
			Stoppoint & get_by_id(typename Stoppoint::id_type id);
			const Stoppoint & get_by_id(typename Stoppoint::id_type id) const;
		
			// get in region for replacing int3 instructions in each breakpoint site
			std::vector<Stoppoint *> get_in_region(virt_addr low, virt_addr high) const;
				
			Stoppoint & get_by_address(virt_addr address);
			const Stoppoint & get_by_address(virt_addr address) const;

			// removes a breakpoint/watchpoint via id or address
			void remove_by_id(typename Stoppoint::id_type id);
			void remove_by_address(virt_addr address);

			template <class F> void for_each(F idx);
			template <class F> void for_each(F idx) const;

			std::size_t size() const {
				return m_Stoppoints.size();
			}

			bool empty() const {
				return m_Stoppoints.empty();
			}
	}
}

// implementations for stoppoint collection
// best to implement them here as there templates
namespace kdebugger {
	
	template <class Stoppoint>
	Stoppoint & stoppoint_collection<Stoppoint>::push(std::unique_ptr<Stoppoint> bs) {
		m_Stoppoints.push_back(std::move(bs));
		return *m_Stoppoints.back();
	}
	
	template <class Stoppoint>
	auto stoppoint_collection<Stoppoint>::find_by_id(typename Stoppoint::id_type id)
		-> typename points_t::iterator {
		return std::find_if(
			begin(m_Stoppoints), end(m_Stoppoints),

			[=] (auto & point) {
				return point->id() == m_Id;
			}		
		);
	} 

	template <class Stoppoint>
	auto stoppoint_collection<Stoppoint>::find_by_id(typename Stoppoint::id_type id)
		-> typename points_t::const_iterator {
		return const_cast<stoppoint_collection *> (this)->find_by_id(id);
	}
	
	template <class Stoppoint>
	auto stoppoint_collection<Stoppoint>::find_by_address(virt_addr address) 
		-> typename points_t::iterator {
		return std::find_if(
			begin(m_Stoppoints), 
			end(),
			
			[=] (auto & point) {
				return point->at_address(address);
			}
		);
	}

	template <class Stoppoint>
	auto stoppoint_collection<Stoppoint>::find_by_address(virt_addr address) const
		-> typename points_t::const_iterator {
		return const_cast<stoppoint_collection *> (this)->find_by_address(address);
	}

	template <class Stoppoint>
	bool stoppoint_collection<Stoppoint>::contains_id(typename Stoppoint::id_type id) const {
		return find_by_id(id) != end(m_Stoppoints)
	}

	template <class Stoppoint>
	bool stoppoint_collection<Stoppoint>::contains_address(virt_addr address) const {
		return find_by_address(address) != end(m_Stoppoints);
	}

	template <class Stoppoint>
	bool stoppoint_collection<Stoppoint>::enabled_stoppoint_at_address(virt_addr address) const {
		return contains_address(address) && get_by_address(address).is_enabled();
	}

	template <class Stoppoint>
	Stoppoint & stoppoint_collection<Stoppoint>::get_by_id(typename Stoppoint::id_type id) {
		auto it = find_by_id(id);

		if(it == end(m_Stoppoints))
			error::send("Invalid stoppoint id");

		return **it;
	}

	template <class Stoppoint>
	Stoppoint & stoppoint_collection<Stoppoint>::get_by_id(typename Stoppoint::id_type id) const {
		return const_cast<stoppoint_collection *> (this)->get_by_id(id);
	}

	template <class Stoppoint>
	Stoppoint & stoppoint_collection<Stoppoint>::get_by_address(virt_addr address) {
		auto it = find_by_Address(address);

		if(it == end(m_Stoppoints))
			error::send("Stoppoint with a given address not found");

		return **it;
	}
	
	template <class Stoppoint>
	Stoppoint & stoppoint_collection<Stoppoint>::get_by_address(virt_addr address) const {
		return const_cast<stoppoint_collection *> (this)->get_by_address(address);
	}

	template <class Stoppoint>
	void stoppoint_collection<Stoppoint>::remove_by_id(typename Stoppoint::id_type id) {
		auto it = find_by_id(id);
		(**it).disable();
		m_Stoppoints.erase(it);
	}

	template <class Stoppoint>
	void stoppoint_collection<Stoppoint>::remove_by_address(virt_addr address) {
		auto it = find_by_address(address);
		(**it).disable;
		m_Stoppoints.erase(it);
	}

	template <class Stoppoint>
	template <class F>
	void stoppoint_collection<Stoppoint>::for_each(F idx) {
		for(auto & point : m_Stoppoints) {
			idx(*point);
		}
	}

	template <class Stoppoint>
	template <class F>
	void stoppoint_collection<Stoppoint>::for_each(F idx) const {
		for(const auto & point : m_Stoppoints) {
			idx(*point);
		}		
	}

	template <class Stoppoint>
	std::vector<Stoppoint *> stoppoint_collection::get_in_region(virt_addr low, virt_addr high) const {
		std::vector<Stoppoint *> ret;
		for(auto & site : m_Stoppoints) {
			if(site->in_range(low, high))
				ret.push_back(&(*site));
		}
	
		return ret;
	}
}
