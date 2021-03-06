//////////////////////////////////////////////////////////////////////////
// PDH Collector 
// 
// Functions from this file collects data from the PDH subsystem and stores 
// it for later use
// *NOTICE* that this is done in a separate thread so threading issues has 
// to be handled. I handle threading issues in the CounterListener's get/
// set accessors.
//
// Copyright (c) 2004 MySolutions NORDIC (http://www.medin.nu)
//
// Date: 2004-03-13
// Author: Michael Medin - <michael@medin.name>
//
// This software is provided "AS IS", without a warranty of any kind.
// You are free to use/modify this code but leave this header intact.
//
//////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "pdh_thread.hpp"
#include <sysinfo.h>
#include "settings.hpp"

#include <nscapi/nscapi_plugin_interface.hpp>
#include <parsers/filter/realtime_helper.hpp>
#include "realtime_data.hpp"

typedef parsers::where::realtime_filter_helper<check_cpu_filter::runtime_data, filters::filter_config_object> cpu_filter_helper;
typedef parsers::where::realtime_filter_helper<check_mem_filter::runtime_data, filters::filter_config_object> mem_filter_helper;


/**
* Thread that collects the data every "CHECK_INTERVAL" seconds.
*
* @param lpParameter Not used
* @return thread exit status
*
* @author mickem
*
* @date 03-13-2004               
*
* @bug If we have "custom named" counters ?
* @bug This whole concept needs work I think.
*
*/
void pdh_thread::thread_proc() {
	if (subsystem == "fast" || subsystem == "auto" || subsystem == "default") {
		PDH::factory::set_native();
	} else if (subsystem == "thread-safe") {
		PDH::factory::set_thread_safe();
	} else {
		NSC_LOG_ERROR_STD("Unknown PDH subsystem valid values are: fast (default) and thread-safe");
	}

	BOOST_FOREACH(PDH::pdh_object obj, configs_) {
		PDH::pdh_instance instance = PDH::factory::create(obj);
		counters_.push_back(instance);
		lookups_[instance->get_name()] = instance;
	}

	PDH::PDHQuery pdh;
	CheckMemory memchecker;

	bool check_pdh = !counters_.empty();

	if (check_pdh) {
		SetThreadLocale(MAKELCID(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),SORT_DEFAULT));
		boost::unique_lock<boost::shared_mutex> writeLock(mutex_, boost::get_system_time() + boost::posix_time::seconds(10));
		if (!writeLock.owns_lock()) {
			NSC_LOG_ERROR_STD("Failed to get mutex when trying to start thread.");
			return;
		}
		pdh.removeAllCounters();
		BOOST_FOREACH(PDH::pdh_instance c, counters_) {
			try {
				if (c->has_instances()) {
					BOOST_FOREACH(PDH::pdh_instance sc, c->get_instances()) { 
						NSC_DEBUG_MSG("Loading counter: " + sc->get_name() + " = " + sc->get_counter());
						pdh.addCounter(sc);
					}
				} else {
					NSC_DEBUG_MSG("Loading counter: " + c->get_name() + " = " + c->get_counter());
					pdh.addCounter(c);
				}
			} catch (...) {
				NSC_LOG_ERROR_WA("EXCEPTION: Failed to load counter: " + c->get_name() + " = ", c->get_counter());
			}
		}
		try {
			pdh.open();
		} catch (const std::exception &e) {
			NSC_LOG_ERROR_EXR("Opening performance counters: ", e);
			return;
		}
	}

	bool has_realtime = !filters_.empty();
	cpu_filter_helper cpu_helper;
	mem_filter_helper mem_helper;
	BOOST_FOREACH(filters::filter_config_object object, filters_.get_object_list()) {
		if (object.check == "memory") {
			check_mem_filter::runtime_data data;
			BOOST_FOREACH(const std::string &d, object.data) {
				data.add(d);
			}
			mem_helper.add_item(object, data);
		} else {
			check_cpu_filter::runtime_data data;
			BOOST_FOREACH(const std::string &d, object.data) {
				data.add(d);
			}
			cpu_helper.add_item(object, data);
		}
	}

	cpu_helper.touch_all();
	mem_helper.touch_all();

	int min_threshold = 10;
	DWORD waitStatus = 0;
	bool first = true;
	int i = 0;

	if (check_pdh)
		NSC_DEBUG_MSG("Checking pdh data");

	mutex_.unlock();

	do {
		std::list<std::string>	errors;
		{
			windows::system_info::cpu_load load = windows::system_info::get_cpu_load();
			boost::unique_lock<boost::shared_mutex> writeLock(mutex_, boost::get_system_time() + boost::posix_time::seconds(5));
			if (!writeLock.owns_lock()) {
				errors.push_back("Failed to get mutex for writing");
			} else {
				try {
					cpu.push(load);
					if (check_pdh)
						pdh.gatherData();

				} catch (const PDH::pdh_exception &e) {
					if (first) {
						// If this is the first run an error will be thrown since the data is not yet available
						// This is "ok" but perhaps another solution would be better, but this works :)
						first = false;
					} else {
						errors.push_back("Failed to query performance counters: " + e.reason());
					}
				} catch (...) {
					errors.push_back("Failed to query performance counters: ");
				}
			} 
		}
		if (has_realtime) {
			if (i++ > min_threshold) {
				cpu_helper.process_items(this);
				mem_helper.process_items(&memchecker);
				i = 0;
			}
		}
		BOOST_FOREACH(const std::string &s, errors) {
			NSC_LOG_ERROR(s);
		}
	} while (((waitStatus = WaitForSingleObject(stop_event_, 1000)) == WAIT_TIMEOUT));
	if (waitStatus != WAIT_OBJECT_0) {
		NSC_LOG_ERROR("Something odd happened when terminating PDH collection thread!");
		return;
	}

	if (check_pdh) 	{
		boost::unique_lock<boost::shared_mutex> writeLock(mutex_, boost::get_system_time() + boost::posix_time::seconds(5));
		if (!writeLock.owns_lock()) {
			NSC_LOG_ERROR("Failed to get Mute when closing thread!");
		}
		try {
			pdh.close();
		} catch (const std::exception &e) {
			NSC_LOG_ERROR_EXR("Failed to close performance counters: ", e);
		}
	}
}

std::map<std::string,long long> pdh_thread::get_int_value(std::string counter) {
	std::map<std::string,long long> ret;
	boost::shared_lock<boost::shared_mutex> readLock(mutex_, boost::get_system_time() + boost::posix_time::seconds(5));
	if (!readLock.owns_lock()) {
		NSC_LOG_ERROR("Failed to get Mutex for: " + counter);
		return ret;
	}

	lookup_type::iterator it = lookups_.find(counter);
	if (it == lookups_.end()) {
		NSC_LOG_ERROR("Counter was not found: " + counter);
		return ret;
	}
	const PDH::pdh_instance ptr = (*it).second;
	if (ptr->has_instances()) {
		BOOST_FOREACH(const PDH::pdh_instance i, ptr->get_instances()) {
			ret[i->get_name()] = i->get_int_value();
		}
	} else {
		ret[ptr->get_name()] = ptr->get_int_value();
	}
	return ret;
}

std::map<std::string,double> pdh_thread::get_value(std::string counter) {
	std::map<std::string,double> ret;
	boost::shared_lock<boost::shared_mutex> readLock(mutex_, boost::get_system_time() + boost::posix_time::seconds(5));
	if (!readLock.owns_lock()) {
		NSC_LOG_ERROR("Failed to get Mutex for: " + counter);
		return ret;
	}

	lookup_type::iterator it = lookups_.find(counter);
	if (it == lookups_.end()) {
		NSC_LOG_ERROR("Counter was not found: " + counter);
		return ret;
	}
	const PDH::pdh_instance ptr = (*it).second;
	if (ptr->has_instances()) {
		BOOST_FOREACH(const PDH::pdh_instance i, ptr->get_instances()) {
			ret[i->get_name()] = i->get_value();
		}
	} else {
		ret[ptr->get_name()] = ptr->get_value();
	}
	return ret;
}

std::map<std::string,double> pdh_thread::get_average(std::string counter, long seconds) {
	std::map<std::string,double> ret;
	boost::shared_lock<boost::shared_mutex> readLock(mutex_, boost::get_system_time() + boost::posix_time::seconds(5));
	if (!readLock.owns_lock()) {
		NSC_LOG_ERROR("Failed to get Mutex for: " + counter);
		return ret;
	}

	lookup_type::iterator it = lookups_.find(counter);
	if (it == lookups_.end()) {
		NSC_LOG_ERROR("Counter was not found: " + counter);
		return ret;
	}
	const PDH::pdh_instance ptr = (*it).second;
	if (ptr->has_instances()) {
		BOOST_FOREACH(const PDH::pdh_instance i, ptr->get_instances()) {
			ret[i->get_name()] = i->get_average(seconds);
		}
	} else {
		ret[ptr->get_name()] = ptr->get_average(seconds);
	}
	return ret;
}

std::map<std::string,windows::system_info::load_entry> pdh_thread::get_cpu_load(long seconds) {
	std::map<std::string,windows::system_info::load_entry> ret;
	windows::system_info::cpu_load load;
	{
		boost::shared_lock<boost::shared_mutex> readLock(mutex_, boost::get_system_time() + boost::posix_time::seconds(5));
		if (!readLock.owns_lock()) {
			NSC_LOG_ERROR("Failed to get Mutex for: cput");
			return ret;
		}
		load = cpu.get_average(seconds);
	}
	ret["total"] = load.total;
	int i=0;
	BOOST_FOREACH(const windows::system_info::load_entry &l, load.core)
		ret["core " + strEx::s::xtos(i++)] = l;
	return ret;
}


bool pdh_thread::start() {
	stop_event_ = CreateEvent(NULL, TRUE, FALSE, _T("EventLogShutdown"));
	thread_ = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&pdh_thread::thread_proc, this)));
	return true;
}
bool pdh_thread::stop() {
	SetEvent(stop_event_);
	if (thread_)
		thread_->join();
	return true;
}

void pdh_thread::add_counter(const PDH::pdh_object &counter) {
	configs_.push_back(counter);
}

void pdh_thread::add_realtime_filter(boost::shared_ptr<nscapi::settings_proxy> proxy, std::string key, std::string query) {
	try {
		filters_.add(proxy, filters_path_, key, query, key == "default");
	} catch (const std::exception &e) {
		NSC_LOG_ERROR_EXR("Failed to add command: " + utf8::cvt<std::string>(key), e);
	} catch (...) {
		NSC_LOG_ERROR_EX("Failed to add command: " + utf8::cvt<std::string>(key));
	}
}
