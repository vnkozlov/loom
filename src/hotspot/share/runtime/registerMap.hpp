/*
 * Copyright (c) 2002, 2021, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_RUNTIME_REGISTERMAP_HPP
#define SHARE_RUNTIME_REGISTERMAP_HPP

#include "code/vmreg.hpp"
#include "oops/stackChunkOop.hpp"
#include "runtime/handles.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"

class JavaThread;

//
// RegisterMap
//
// A companion structure used for stack traversal. The RegisterMap contains
// misc. information needed in order to do correct stack traversal of stack
// frames.  Hence, it must always be passed in as an argument to
// frame::sender(RegisterMap*).
//
// In particular,
//   1) It provides access to the thread for which the stack belongs.  The
//      thread object is needed in order to get sender of a deoptimized frame.
//
//   2) It is used to pass information from a callee frame to its caller
//      frame about how the frame should be traversed.  This is used to let
//      the caller frame take care of calling oops-do of out-going
//      arguments, when the callee frame is not instantiated yet.  This
//      happens, e.g., when a compiled frame calls into
//      resolve_virtual_call.  (Hence, it is critical that the same
//      RegisterMap object is used for the entire stack walk.  Normally,
//      this is hidden by using the StackFrameStream.)  This is used when
//      doing follow_oops and oops_do.
//
//   3) The RegisterMap keeps track of the values of callee-saved registers
//      from frame to frame (hence, the name).  For some stack traversal the
//      values of the callee-saved registers does not matter, e.g., if you
//      only need the static properties such as frame type, pc, and such.
//      Updating of the RegisterMap can be turned off by instantiating the
//      register map as: RegisterMap map(thread, false);

class RegisterMap : public StackObj {
 public:
    typedef julong LocationValidType;
  enum {
    reg_count = ConcreteRegisterImpl::number_of_registers,
    location_valid_type_size = sizeof(LocationValidType)*8,
    location_valid_size = (reg_count+location_valid_type_size-1)/location_valid_type_size
  };
 private:
  intptr_t*         _location[reg_count];     // Location of registers (intptr_t* looks better than address in the debugger)
  LocationValidType _location_valid[location_valid_size];
  bool              _include_argument_oops;   // Should include argument_oop marked locations for compiler
  JavaThread*       _thread;                  // Reference to current thread
  stackChunkHandle  _chunk;                   // The current continuation stack chunk, if any
  int               _chunk_index;             // incremented whenever a new chunk is set

  bool              _update_map;              // Tells if the register map need to be
                                              // updated when traversing the stack
  bool              _process_frames;          // Should frames be processed by stack watermark barriers?
  bool              _walk_cont;               // whether to walk frames on a continuation stack

  NOT_PRODUCT(bool  _skip_missing;) // ignore missing registers
  NOT_PRODUCT(bool  _async;)        // walking frames asynchronously, at arbitrary points

#ifdef ASSERT
  void check_location_valid();
#else
  void check_location_valid() {}
#endif

 public:
  DEBUG_ONLY(intptr_t* _update_for_id;) // Assert that RegisterMap is not updated twice for same frame
  RegisterMap(JavaThread *thread, bool update_map = true, bool process_frames = true, bool walk_cont = false);
  RegisterMap(oop continuation, bool update_map = true);
  RegisterMap(const RegisterMap* map);

  address location(VMReg reg, intptr_t* sp) const {
    int index = reg->value() / location_valid_type_size;
    assert(0 <= reg->value() && reg->value() < reg_count, "range check");
    assert(0 <= index && index < location_valid_size, "range check");
    if (_location_valid[index] & ((LocationValidType)1 << (reg->value() % location_valid_type_size))) {
      return (address) _location[reg->value()];
    } else {
      return pd_location(reg);
    }
  }

  address location(VMReg base_reg, int slot_idx) const {
    if (slot_idx > 0) {
      return pd_location(base_reg, slot_idx);
    } else {
      return location(base_reg, nullptr);
    }
  }

  address trusted_location(VMReg reg) const {
    return (address) _location[reg->value()];
  }

  void verify(RegisterMap& other) {
    for (int i = 0; i < reg_count; ++i) {
      assert(_location[i] == other._location[i], "");
    }
  }

  void set_location(VMReg reg, address loc) {
    int index = reg->value() / location_valid_type_size;
    assert(0 <= reg->value() && reg->value() < reg_count, "range check");
    assert(0 <= index && index < location_valid_size, "range check");
    assert(_update_map, "updating map that does not need updating");
    _location[reg->value()] = (intptr_t*) loc;
    _location_valid[index] |= ((LocationValidType)1 << (reg->value() % location_valid_type_size));
    check_location_valid();
  }

  // Called by an entry frame.
  void clear();

  bool include_argument_oops() const      { return _include_argument_oops; }
  void set_include_argument_oops(bool f)  { _include_argument_oops = f; }

  JavaThread *thread()  const { return _thread; }
  bool update_map()     const { return _update_map; }
  bool process_frames() const { return _process_frames; }
  bool walk_cont()      const { return _walk_cont; }

  void set_walk_cont(bool value) { _walk_cont = value; }

  bool in_cont()        const { return _chunk() != NULL; } // Whether we are currently on the hstack; if true, frames are relativized
  oop cont() const;
  stackChunkHandle stack_chunk() const { return _chunk; }
  void set_stack_chunk(stackChunkOop chunk);
  int stack_chunk_index() const { return _chunk_index; }
  void set_stack_chunk_index(int index) { _chunk_index = index; }

  const RegisterMap* as_RegisterMap() const { return this; }
  RegisterMap* as_RegisterMap() { return this; }

  void print_on(outputStream* st) const;
  void print() const;

  void set_async(bool value)        { NOT_PRODUCT(_async = value;) }
  void set_skip_missing(bool value) { NOT_PRODUCT(_skip_missing = value;) }

#ifndef PRODUCT
  bool is_async() const             { return _async; }
  bool should_skip_missing() const  { return _skip_missing; }

  VMReg find_register_spilled_here(void* p, intptr_t* sp);
#endif

  // the following contains the definition of pd_xxx methods
#include CPU_HEADER(registerMap)

};

#endif // SHARE_RUNTIME_REGISTERMAP_HPP
