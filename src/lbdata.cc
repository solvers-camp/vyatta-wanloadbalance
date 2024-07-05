/*
 * Module: lbdata.cc
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#include <stdio.h>
#include <sys/time.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/udp.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <iostream>

#include "rl_str_proc.hh"
#include "lbtest.hh"
#include "lbdata.hh"

int LBHealthHistory::_buffer_size = 10;


/**
 *
 *
 **/
void
LBHealth::put(int rtt)
{
  int activity_ct = _hresults.push(rtt);

  if (rtt == -1) {
    if (activity_ct >= _failure_ct) {
      if (_is_active == true) {
	_state_changed = true;
      }
      _is_active = false;
    }
  }
  else {
    if (activity_ct >= _success_ct) {
      if (_is_active == false) {
	_state_changed = true;
      }
      _is_active = true;
    }
  }
}


/**
 *
 *
 **/
void
LBHealth::start_new_test_cycle()
{
  //let's first clear out all the old results                                                                                                                 
  TestIter t = _test_coll.begin();
  while (t != _test_coll.end()) {
    t->second->_state = LBTest::K_NONE;
    ++t;
  }
  _test_iter = _test_coll.begin();
  //  _test_iter->second->start();
}

/**
 *
 *
 **/
void
LBHealth::start_new_test()
{
  if (_test_iter == _test_coll.end()) {
    return;
  }
  _test_iter->second->start();
}

/**
 *
 *
 **/
void
LBHealth::send_test()
{
  if (_test_iter == _test_coll.end()) {
    return; //means we are done
  }
  _test_iter->second->send(*this);
}

/**
 *
 *
 **/
int
LBHealth::recv_test()
{
  if (_test_iter == _test_coll.end()) {
    put(-1);
    return 0; //means stop iteration, no more tests...
  }
  int rtt = _test_iter->second->recv(*this);
  ++_test_iter;
  if (rtt >= 0) {
    put(rtt);
    return 0; //means success, testing is done
  }
  return -1; //means testing is not done for this interface
}

/**
 *
 *
 **/
LBHealthHistory::LBHealthHistory(int buffer_size) :
  _last_success(0),
  _last_failure(0),
  _failure_count(0),
  _index(0)
{
  _resp_data.resize(10);

  for (int i = 0; i < _buffer_size; ++i) {
    _resp_data[i] = 0;
  }
}

/**
 *
 *
 **/
int
LBHealthHistory::get_last_resp()
{
  if (_resp_data.size() == 0) {
    return -1;
  }
  return (_resp_data[0]);
}

/**
 *
 *
 **/
int
LBHealthHistory::push(int rtt)
{
  struct timeval tv;
  gettimeofday(&tv,NULL);

  if (rtt == -1) {
    _last_failure = tv.tv_sec;
    ++_failure_count;
  }
  else {
    _failure_count = 0;
    _last_success = tv.tv_sec;
  }

  _resp_data[_index % _buffer_size] = rtt;
  ++_index;

  //compute count of sequence of same responses
  int ct = 0;
  int start_index = (_index - 1) % _buffer_size;
  for (int i = 0; i < _buffer_size; ++i) {
    int index = start_index - i;
    if (index < 0) {
      //handle wrap around of index here
      index = _buffer_size - index;
    }

    if (_resp_data[index] == -1 && rtt == -1) {
      ++ct;
    }
    else if (_resp_data[index] != -1 && rtt != -1) {
      ++ct;
    }
    else {
      break;
    }
  }
  
  return ct;
}

/**
 *
 *
 **/
bool
LBData::is_active(const string &iface) 
{
  InterfaceHealthIter iter = _iface_health_coll.find(iface);
  if (iter != _iface_health_coll.end()) {
    return iter->second._is_active;
  }
  return false;
}

/**
 *
 *
 **/
void
LBData::dump()
{

  cout << "health:" << endl;
  LBData::InterfaceHealthIter h_iter = _iface_health_coll.begin();
  while (h_iter != _iface_health_coll.end()) {
    cout << "  interface: " << h_iter->first << endl;
    cout << "    nexthop: " << h_iter->second._nexthop << endl;
    cout << "    success ct: " << h_iter->second._success_ct << endl;
    cout << "    failure ct: " << h_iter->second._failure_ct << endl;
    LBHealth::TestIter t_iter = h_iter->second._test_coll.begin();
    while (t_iter != h_iter->second._test_coll.end()) {
      cout << "    test: " << t_iter->first << endl;
      string foo = t_iter->second->dump();
      cout << "      " << foo << endl;
      ++t_iter;
    }
    ++h_iter;
  }

  cout << endl << "wan:" << endl;
  LBData::LBRuleIter r_iter = _lb_rule_coll.begin();
  while (r_iter != _lb_rule_coll.end()) {
    cout << "  rule: " << r_iter->first << endl;
    cout << "    " << r_iter->second._proto << endl;
    cout << "    " << r_iter->second._s_addr << endl;
    cout << "    " << r_iter->second._s_port << endl;

    cout << "    " << r_iter->second._d_addr << endl;
    cout << "    " << r_iter->second._d_port << endl;
    
    if (r_iter->second._limit) {
      cout << "     limit:" << endl;
      cout << "       burst: " << r_iter->second._limit_burst << endl;
      cout << "       rate: " << r_iter->second._limit_burst << endl;
      if (r_iter->second._limit_mode) {
	cout << "       thresh: above" << endl;
      }
      else { 
	cout << "       thresh: below" << endl;
      }
      if (r_iter->second._limit_period == LBRule::K_SECOND) {
	cout << "       period: second" << endl;
      }
      else if (r_iter->second._limit_period == LBRule::K_MINUTE) {
	cout << "       period: minute" << endl;
      }
      else if (r_iter->second._limit_period == LBRule::K_HOUR) {
	cout << "       period: hour" << endl;
      }
    }
    LBRule::InterfaceDistIter ri_iter = r_iter->second._iface_dist_coll.begin();
    while (ri_iter != r_iter->second._iface_dist_coll.end()) {
      cout << "      interface: " << ri_iter->first << endl;
      cout << "      weight: " << ri_iter->second << endl;
      ++ri_iter;
    }
    ++r_iter;
  }
  cout << "end dump" << endl;
}

/**
 *
 *
 **/
map<string,string>
LBData::state_changed()
{
  map<string,string> coll;
  LBData::InterfaceHealthIter h_iter = _iface_health_coll.begin();
  while (h_iter != _iface_health_coll.end()) {
    if (h_iter->second.state_changed()) {
      string tmp = (h_iter->second._is_active ? string("ACTIVE") : string("FAILED"));      
      
      struct timeval tv;
      gettimeofday(&tv,NULL);
      h_iter->second._last_time_state_changed = (unsigned long)tv.tv_sec;

      syslog(LOG_WARNING, "Interface %s has changed state to %s",h_iter->first.c_str(),tmp.c_str());

      coll.insert(pair<string,string>(h_iter->first,tmp));
    }
    ++h_iter;
  }
  return coll;
}

/**
 *
 *
 **/
void
LBData::reset_state_changed()
{
  LBData::InterfaceHealthIter h_iter = _iface_health_coll.begin();
  while (h_iter != _iface_health_coll.end()) {
    h_iter->second._state_changed = false;
    ++h_iter;
  }
}

/**
 *
 *
 **/
void
LBData::update_dhcp_nexthop()
{
  /**
   * currently only reads the nexthop as maintained by the dhcp client
   **/
  LBData::InterfaceHealthIter h_iter = _iface_health_coll.begin();
  while (h_iter != _iface_health_coll.end()) {
    if (h_iter->second._nexthop == "dhcp") {
      string file("/run/dhclient/dhclient_"+h_iter->first+".lease");
      FILE *fp = fopen(file.c_str(),"r");
      if (fp) {
	char str[1025];
	while (fgets(str, 1024, fp)) {
	  StrProc tokens(str, "=");
	  if (tokens.get(0) == "new_routers") {
	    string tmp = tokens.get(1);
	    
	    //need to watch out for the case where there are mult routers...
	    StrProc tokens2(tmp," ");
	    tmp = tokens2.get(0);

	    long len = tmp.length()-2;

	    h_iter->second._dhcp_nexthop = tmp.substr(1,len);
	    break;
	  }
	}
	fclose(fp);
      }
      else {
	//check if this is a ppp interface
	string pppfile("/var/run/load-balance/ppp/"+h_iter->first);
	FILE *fp = fopen(pppfile.c_str(),"r");
	if (fp) {
	  char str[1025];
	  if (fgets(str, 1024, fp)) {
	    h_iter->second._dhcp_nexthop = string(str);
	  }
	  fclose(fp);
	}
      }
    }
    ++h_iter;
  }
}

