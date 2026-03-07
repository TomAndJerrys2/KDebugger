#pragma once

// general header includes
#include <cstdint>
#include <cstddef>

// Project specific headers
#include <libkdebugger/types.hpp>

namespace kdebugger {

    class process;

    class watchpoint {
       
        private:
            friend process;

            // private constructor
            watchpoint(process & proc, virt_addr address, stoppoint_mode mode, std::size_t size);

            // member variables
            id_type m_Id;
            process * m_Process {nullptr};
            virt_addr m_Address;
            stoppoint_mode m_Mode;
            std::size_t m_Size;
            bool m_isEnabled;
            int m_HardwareRegisterIndex = -1;
            
            std::uint64_t m_Data = 0;
            std::uint64_t m_PreviousData = 0;

        public:
            // delete default constructor
            watchpoint() = delete;

            // delete copy constructor and copy assignment 
            watchpoint(const watchpoint &) = delete;
            watchpoint & operator = (const watchpoint &) = delete;
    
            // enables a given watchpoint
            void enable();

            // disables a given watchpoint
            void disable();

            // --- getters ---

            bool is_enabled() const {
                return m_isEnabled;
            }

            virt_addr address() const {
                return m_Address;
            }

            stoppoint_mode mode() const {
                return m_Mode;
            }

            std::size_t size() const {
                return m_Size;
            }

            bool at_address(virt_addr addr) const {
                return m_Address == addr;
            }

            bool in_range(virt_addr low, virt_addr high) const {
                return low <= m_Address && high > m_Address;
            }

            std::uint64_t data() const {
                return m_Data;
            }

            std::uint64_t previous_data() const {
                return m_PreviousData;
            }

            void update_data();
    };
}
