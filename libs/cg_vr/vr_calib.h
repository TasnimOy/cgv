#pragma once

#include <string>
#include <map>
#include <vr/vr_state.h>
#include <vr/vr_driver.h>

#include "lib_begin.h"

///@ingroup VR
///@{

///
namespace cgv {
	namespace gui {
		/// vr key events use the key codes defined in vr::VRKeys
		class CGV_API vr_calibration
		{
		protected:
			std::map<std::string, std::map<std::string, vr::vr_trackable_state> > calibration_info;
			/// iterate vr drivers and copy calibration information into map
			void update_calibration_info();
			/// update the calibration of a driver from the given target reference states
			bool update_driver_calibration(vr::vr_driver* dp, const std::map<std::string, vr::vr_trackable_state>& target_reference_states) const;
			/// constructor is protected to forbid construction of more than one singleton
			vr_calibration();
			/// allow ref_vr_calibration() function to construct one instance
			friend extern CGV_API vr_calibration& ref_vr_calibration();
		public:
			bool read_calibration(const std::string& file_path);
			bool write_calibration(const std::string& file_path);
		};

		/// access to singleton object of vr_calibration class
		extern CGV_API vr_calibration& ref_vr_calibration();
	}
}

///@}

#include <cgv/config/lib_end.h>
