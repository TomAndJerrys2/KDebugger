#pragma once

// general header includes
#include <vector>
#include <memory>

// Private / project-specific headers
#include <libkdebugger/types.hpp>

namespace kdebugger {

	template <class Stoppoint>
	class stoppoint_collection {
		
		private:
			using points_t = std::vector<std::unique_ptr<Stoppoint>>;
			points_t m_Stoppoints;

		public:
			Stoppoint & push(std::unique_ptr<Stoppoint> bs);

			// checks if the collection contains a stop point either
			// matching a given Id or a virtual address
			bool contains_id(typename Stoppoint::id_type id) const;
			bool contains_address(virt_addr address) const;
			bool enable_stoppoint_at_address(virt_addr address) const;
	
			// gets a stop point by its id -> overload for returning constant
			// l-value reference
			Stoppoint & get_by_id(typename Stoppoint::id_type id);
			const Stoppoint & get_by_id(typename Stoppoint::id_type id) const;
		
			
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
