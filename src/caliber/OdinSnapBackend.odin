package audacity_backend

import "core:math"
import "core:testing"
import "base:runtime"

Caliber_Status :: i32

CALIBER_STATUS_OK                 :: Caliber_Status(0)
CALIBER_STATUS_INVALID_ARGUMENT   :: Caliber_Status(1)
CALIBER_STATUS_BUFFER_TOO_SMALL   :: Caliber_Status(3)
CALIBER_STATUS_LIMIT_EXCEEDED     :: Caliber_Status(4)
CALIBER_STATUS_UNAVAILABLE        :: Caliber_Status(7)
CALIBER_STATUS_QUEUE_FULL         :: Caliber_Status(8)
CALIBER_STATUS_UNSUPPORTED_VERSION :: Caliber_Status(9)
CALIBER_STATUS_INTERNAL           :: Caliber_Status(10)

SNAP_MODE_OFF :: u32(0)
SNAP_MODE_NEAREST :: u32(1)
SNAP_MODE_PRIOR :: u32(2)

SNAP_ACTION_SET_ENABLED :: u32(1)
SNAP_ACTION_SET_MODE :: u32(2)

COMMAND_SIZE :: 24
STATE_HEADER_SIZE :: 40
MAX_SNAP_TO_BYTES :: 1024
MAX_STATE_SIZE :: STATE_HEADER_SIZE + MAX_SNAP_TO_BYTES
STATE_SCHEMA :: u32(0x41535301)

Caliber_Context :: struct {}

Caliber_Context_Config :: struct {
	struct_size:          u32,
	max_command_bytes:    uintptr,
	max_publication_bytes: uintptr,
	max_resource_bytes:   uintptr,
	max_resources:        uintptr,
	telemetry_width:      uintptr,
	max_pending_commands: uintptr,
}

Caliber_State_Publication :: struct {
	revision: u64,
	schema:   u32,
	reserved: u32,
	data:     [^]u8,
	len:      uintptr,
	lease:    rawptr,
}

// This is the ABI-v1 prefix through state_publication_release. The Caliber
// header remains the canonical declaration; C++ checks this prefix size.
Caliber_Api_V1_Prefix :: struct {
	abi_version:                   u32,
	struct_size:                   u32,
	context_create:                proc "c" (config: ^Caliber_Context_Config, out_context: ^^Caliber_Context) -> Caliber_Status,
	context_destroy:               proc "c" (ctx: ^Caliber_Context),
	context_dispatch:              proc "c" (ctx: ^Caliber_Context, data: [^]u8, len: uintptr) -> Caliber_Status,
	context_peek_command:          proc "c" (ctx: ^Caliber_Context, required: ^uintptr) -> Caliber_Status,
	context_take_command:          proc "c" (ctx: ^Caliber_Context, out_data: [^]u8, capacity: uintptr, out_len: ^uintptr) -> Caliber_Status,
	context_publish_state:         proc "c" (ctx: ^Caliber_Context, schema: u32, data: [^]u8, len: uintptr, out_revision: ^u64) -> Caliber_Status,
	context_read_latest_state:     proc "c" (ctx: ^Caliber_Context, out_publication: ^Caliber_State_Publication) -> Caliber_Status,
	state_publication_release:     proc "c" (publication: ^Caliber_State_Publication),
}

Snap_State_V1 :: struct {
	project_id:      u64,
	mode:             u32,
	reserved:         u32,
	selection_start:  f64,
	selection_end:    f64,
	snap_to_data:     [^]u8,
	snap_to_len:      uintptr,
}

Snap_Set_Mode_Proc :: proc "c" (user_data: rawptr, mode: u32) -> Caliber_Status
Snap_Read_State_Proc :: proc "c" (user_data: rawptr, out_state: ^Snap_State_V1) -> Caliber_Status

Snap_Backend_Session :: struct {
	api:              ^Caliber_Api_V1_Prefix,
	ctx:          ^Caliber_Context,
	project_id:       u64,
	status:           Caliber_Status,
	pending:          bool,
	pumping:          bool,
	expected_action:  u32,
	expected_value:   u32,
	revision:         u64,
	command:          [COMMAND_SIZE]u8,
	published:        [MAX_STATE_SIZE]u8,
	published_len:    int,
}

@(private="file")
put_le :: proc(data: []u8, at, count: int, value: u64) {
	for i in 0..<count {
		data[at+i] = u8(value >> u64(i*8))
	}
}

@(private="file")
get_le :: proc(data: []u8, at, count: int) -> u64 {
	value: u64
	for i in 0..<count {
		value |= u64(data[at+i]) << u64(i*8)
	}
	return value
}

@(private="file")
put_f64_le :: proc(data: []u8, at: int, value: f64) {
	put_le(data, at, 8, transmute(u64)value)
}

@(private="file")
get_f64_le :: proc(data: []u8, at: int) -> f64 {
	return transmute(f64)get_le(data, at, 8)
}

@(private="file")
encode_command :: proc(project_id: u64, action, value: u32, out: []u8) -> Caliber_Status {
	if project_id == 0 || len(out) != COMMAND_SIZE {
		return CALIBER_STATUS_INVALID_ARGUMENT
	}
	if action == SNAP_ACTION_SET_ENABLED {
		if value > 1 do return CALIBER_STATUS_INVALID_ARGUMENT
	} else if action == SNAP_ACTION_SET_MODE {
		if value > SNAP_MODE_PRIOR do return CALIBER_STATUS_INVALID_ARGUMENT
	} else {
		return CALIBER_STATUS_INVALID_ARGUMENT
	}
	for i in 0..<COMMAND_SIZE do out[i] = 0
	copy(out[0:4], "ASCM")
	put_le(out, 4, 2, 2)
	put_le(out, 6, 2, u64(action))
	put_le(out, 8, 8, project_id)
	put_le(out, 16, 4, u64(value))
	return CALIBER_STATUS_OK
}

@(private="file")
decode_command :: proc(project_id: u64, expected_action, expected_value: u32, data: []u8) -> (Caliber_Status, u32) {
	if project_id == 0 || len(data) != COMMAND_SIZE ||
		data[0] != 'A' || data[1] != 'S' || data[2] != 'C' || data[3] != 'M' ||
		get_le(data, 4, 2) != 2 ||
		get_le(data, 6, 2) != u64(expected_action) ||
		get_le(data, 8, 8) != project_id ||
		get_le(data, 16, 4) != u64(expected_value) ||
		get_le(data, 20, 4) != 0 {
		return CALIBER_STATUS_INVALID_ARGUMENT, SNAP_MODE_OFF
	}
	if expected_action == SNAP_ACTION_SET_ENABLED {
		if expected_value > 1 do return CALIBER_STATUS_INVALID_ARGUMENT, SNAP_MODE_OFF
		if expected_value == 0 do return CALIBER_STATUS_OK, SNAP_MODE_OFF
		return CALIBER_STATUS_OK, SNAP_MODE_NEAREST
	}
	if expected_action == SNAP_ACTION_SET_MODE && expected_value <= SNAP_MODE_PRIOR {
		return CALIBER_STATUS_OK, expected_value
	}
	return CALIBER_STATUS_INVALID_ARGUMENT, SNAP_MODE_OFF
}

@(private="file")
invalidate :: proc(session: ^Snap_Backend_Session, status: Caliber_Status) {
	session.status = status
	session.pending = false
	if session.ctx != nil {
		session.api.context_destroy(session.ctx)
		session.ctx = nil
	}
}

@(private="file")
read_publication :: proc(session: ^Snap_Backend_Session, publication: ^Caliber_State_Publication, state: ^Snap_State_V1) -> bool {
	if publication.schema != STATE_SCHEMA || publication.reserved != 0 ||
		publication.revision != session.revision || publication.len != uintptr(session.published_len) ||
		publication.len < uintptr(STATE_HEADER_SIZE) || publication.data == nil {
		return false
	}
	for i in 0..<session.published_len {
		if publication.data[i] != session.published[i] do return false
	}
	data := publication.data[:publication.len]
	if data[0] != 'A' || data[1] != 'S' || data[2] != 'S' || data[3] != 'T' ||
		get_le(data, 4, 2) != 1 || get_le(data, 6, 2) != 0 ||
		get_le(data, 8, 8) != session.project_id || get_le(data, 16, 4) > u64(SNAP_MODE_PRIOR) {
		return false
	}
	snap_to_len := get_le(data, 20, 4)
	if snap_to_len > u64(MAX_SNAP_TO_BYTES) || uintptr(snap_to_len) != publication.len-uintptr(STATE_HEADER_SIZE) {
		return false
	}
	start := get_f64_le(data, 24)
	end := get_f64_le(data, 32)
	if math.is_nan(start) || math.is_inf(start) || math.is_nan(end) || math.is_inf(end) || start > end {
		return false
	}
	state^ = Snap_State_V1{
		project_id = session.project_id,
		mode = u32(get_le(data, 16, 4)),
		selection_start = start,
		selection_end = end,
		snap_to_data = publication.data[STATE_HEADER_SIZE:],
		snap_to_len = uintptr(snap_to_len),
	}
	return true
}

@(export, link_name="audacity_odin_snap_backend_session_size")
odin_session_size :: proc "c" () -> uintptr {
	return uintptr(size_of(Snap_Backend_Session))
}

@(export, link_name="audacity_odin_snap_backend_api_prefix_size")
odin_api_prefix_size :: proc "c" () -> uintptr {
	return uintptr(size_of(Caliber_Api_V1_Prefix))
}

@(export, link_name="audacity_odin_snap_backend_state_v1_size")
odin_state_v1_size :: proc "c" () -> uintptr {
	return uintptr(size_of(AudacityOdinSnapStateV1))
}

@(export, link_name="audacity_odin_snap_backend_session_init")
odin_session_init :: proc "c" (storage: rawptr, storage_size: uintptr, project_id: u64, api_raw: rawptr) -> Caliber_Status {
	context = runtime.default_context()
	if storage == nil || project_id == 0 || storage_size < uintptr(size_of(Snap_Backend_Session)) || uintptr(storage) % uintptr(size_of(u64)) != 0 {
		return CALIBER_STATUS_INVALID_ARGUMENT
	}
	if api_raw == nil do return CALIBER_STATUS_UNSUPPORTED_VERSION
	api := cast(^Caliber_Api_V1_Prefix)api_raw
	if api.abi_version != 1 || api.struct_size < u32(size_of(Caliber_Api_V1_Prefix)) ||
		api.context_create == nil || api.context_destroy == nil || api.context_dispatch == nil ||
		api.context_take_command == nil || api.context_publish_state == nil ||
		api.context_read_latest_state == nil || api.state_publication_release == nil {
		return CALIBER_STATUS_UNSUPPORTED_VERSION
	}
	session := cast(^Snap_Backend_Session)storage
	session^ = Snap_Backend_Session{api=api, project_id=project_id, status=CALIBER_STATUS_INTERNAL}
	config := Caliber_Context_Config{
		struct_size = u32(size_of(Caliber_Context_Config)),
		max_command_bytes = uintptr(COMMAND_SIZE),
		max_publication_bytes = uintptr(MAX_STATE_SIZE),
		max_pending_commands = 2,
	}
	status := api.context_create(&config, &session.ctx)
	if status == CALIBER_STATUS_OK && session.ctx == nil do status = CALIBER_STATUS_INTERNAL
	if status != CALIBER_STATUS_OK {
		if session.ctx != nil {
			api.context_destroy(session.ctx)
			session.ctx = nil
		}
		session.status = status
		return status
	}
	session.status = CALIBER_STATUS_OK
	return CALIBER_STATUS_OK
}

@(export, link_name="audacity_odin_snap_backend_session_deinit")
odin_session_deinit :: proc "c" (storage: rawptr) {
	context = runtime.default_context()
	if storage == nil do return
	session := cast(^Snap_Backend_Session)storage
	if session.ctx != nil do session.api.context_destroy(session.ctx)
	session^ = Snap_Backend_Session{}
}

@(export, link_name="audacity_odin_snap_backend_dispatch")
odin_dispatch :: proc "c" (storage: rawptr, action, value: u32) -> Caliber_Status {
	context = runtime.default_context()
	if storage == nil do return CALIBER_STATUS_INVALID_ARGUMENT
	session := cast(^Snap_Backend_Session)storage
	if session.ctx == nil do return session.status
	if session.pumping do return CALIBER_STATUS_UNAVAILABLE
	if session.pending {
		invalidate(session, CALIBER_STATUS_QUEUE_FULL)
		return session.status
	}
	status := encode_command(session.project_id, action, value, session.command[:])
	if status != CALIBER_STATUS_OK do return status
	status = session.api.context_dispatch(session.ctx, raw_data(session.command[:]), uintptr(COMMAND_SIZE))
	if status != CALIBER_STATUS_OK {
		invalidate(session, status)
		return status
	}
	session.pending = true
	session.expected_action = action
	session.expected_value = value
	return CALIBER_STATUS_OK
}

@(export, link_name="audacity_odin_snap_backend_pump")
odin_pump :: proc "c" (storage, user_data: rawptr, set_mode: Snap_Set_Mode_Proc, read_state: Snap_Read_State_Proc, apply_started: ^u8) -> Caliber_Status {
	context = runtime.default_context()
	if apply_started != nil do apply_started^ = 0
	if storage == nil || user_data == nil || set_mode == nil || read_state == nil || apply_started == nil {
		return CALIBER_STATUS_INVALID_ARGUMENT
	}
	session := cast(^Snap_Backend_Session)storage
	if session.ctx == nil do return session.status
	if session.pumping || !session.pending do return CALIBER_STATUS_UNAVAILABLE
	session.pumping = true
	defer session.pumping = false
	received: [COMMAND_SIZE]u8
	received_len: uintptr
	status := session.api.context_take_command(session.ctx, raw_data(received[:]), uintptr(len(received)), &received_len)
	if status != CALIBER_STATUS_OK {
		invalidate(session, status)
		return status
	}
	session.pending = false
	if received_len != uintptr(COMMAND_SIZE) {
		invalidate(session, CALIBER_STATUS_INVALID_ARGUMENT)
		return session.status
	}
	decode_status, mode := decode_command(session.project_id, session.expected_action, session.expected_value, received[:])
	if decode_status != CALIBER_STATUS_OK {
		invalidate(session, decode_status)
		return decode_status
	}
	apply_started^ = 1
	status = set_mode(user_data, mode)
	if status != CALIBER_STATUS_OK {
		invalidate(session, status)
		return status
	}
	state: Snap_State_V1
	status = read_state(user_data, &state)
	if status != CALIBER_STATUS_OK {
		invalidate(session, status)
		return status
	}
	if state.snap_to_len > uintptr(MAX_SNAP_TO_BYTES) {
		invalidate(session, CALIBER_STATUS_LIMIT_EXCEEDED)
		return session.status
	}
	if state.project_id != session.project_id || state.mode > SNAP_MODE_PRIOR || state.reserved != 0 ||
		(state.snap_to_len > 0 && state.snap_to_data == nil) ||
		math.is_nan(state.selection_start) || math.is_inf(state.selection_start) ||
		math.is_nan(state.selection_end) || math.is_inf(state.selection_end) ||
		state.selection_start > state.selection_end {
		invalidate(session, CALIBER_STATUS_INVALID_ARGUMENT)
		return session.status
	}
	state_len := STATE_HEADER_SIZE + int(state.snap_to_len)
	for i in 0..<MAX_STATE_SIZE do session.published[i] = 0
	copy(session.published[0:4], "ASST")
	put_le(session.published[:], 4, 2, 1)
	put_le(session.published[:], 8, 8, session.project_id)
	put_le(session.published[:], 16, 4, u64(state.mode))
	put_le(session.published[:], 20, 4, u64(state.snap_to_len))
	put_f64_le(session.published[:], 24, state.selection_start)
	put_f64_le(session.published[:], 32, state.selection_end)
	for i in 0..<int(state.snap_to_len) {
		session.published[STATE_HEADER_SIZE+i] = state.snap_to_data[i]
	}
	revision: u64
	status = session.api.context_publish_state(session.ctx, STATE_SCHEMA, raw_data(session.published[:state_len]), uintptr(state_len), &revision)
	if status != CALIBER_STATUS_OK || revision <= session.revision {
		if status == CALIBER_STATUS_OK do status = CALIBER_STATUS_INTERNAL
		invalidate(session, status)
		return status
	}
	session.revision = revision
	session.published_len = state_len
	return CALIBER_STATUS_OK
}

@(export, link_name="audacity_odin_snap_backend_read_latest")
odin_read_latest :: proc "c" (storage: rawptr, out_state: ^AudacityOdinSnapStateV1, snap_to_buffer: [^]u8, snap_to_capacity: uintptr, snap_to_required: ^uintptr, revision: ^u64, has_state: ^u8) -> Caliber_Status {
	context = runtime.default_context()
	if has_state != nil do has_state^ = 0
	if storage == nil || out_state == nil || snap_to_required == nil || revision == nil || has_state == nil {
		return CALIBER_STATUS_INVALID_ARGUMENT
	}
	session := cast(^Snap_Backend_Session)storage
	if session.ctx == nil do return session.status
	if session.pumping || session.revision == 0 do return CALIBER_STATUS_UNAVAILABLE
	publication: Caliber_State_Publication
	status := session.api.context_read_latest_state(session.ctx, &publication)
	if status != CALIBER_STATUS_OK {
		invalidate(session, status)
		return status
	}
	defer session.api.state_publication_release(&publication)
	state: Snap_State_V1
	if !read_publication(session, &publication, &state) {
		invalidate(session, CALIBER_STATUS_INVALID_ARGUMENT)
		return session.status
	}
	if snap_to_capacity < state.snap_to_len {
		snap_to_required^ = state.snap_to_len
		return CALIBER_STATUS_BUFFER_TOO_SMALL
	}
	if state.snap_to_len > 0 && snap_to_buffer == nil do return CALIBER_STATUS_INVALID_ARGUMENT
	for i in 0..<int(state.snap_to_len) {
		snap_to_buffer[i] = state.snap_to_data[i]
	}
	snap_to_required^ = state.snap_to_len
	revision^ = publication.revision
	out_state^ = AudacityOdinSnapStateV1{
		project_id = state.project_id,
		mode = state.mode,
		selection_start = state.selection_start,
		selection_end = state.selection_end,
		snap_to_data = snap_to_buffer,
		snap_to_len = state.snap_to_len,
	}
	has_state^ = 1
	return CALIBER_STATUS_OK
}

@(export, link_name="audacity_odin_snap_backend_invalidate")
odin_invalidate :: proc "c" (storage: rawptr, status: Caliber_Status) {
	context = runtime.default_context()
	if storage != nil {
		session := cast(^Snap_Backend_Session)storage
		invalidate(session, status)
	}
}

AudacityOdinSnapStateV1 :: struct {
	project_id:      u64,
	mode:             u32,
	reserved:         u32,
	selection_start:  f64,
	selection_end:    f64,
	snap_to_data:     [^]u8,
	snap_to_len:      uintptr,
}

@(test)
test_snap_command_actions :: proc(t: ^testing.T) {
	bytes: [COMMAND_SIZE]u8
	testing.expect(t, encode_command(41, SNAP_ACTION_SET_ENABLED, 1, bytes[:]) == CALIBER_STATUS_OK)
	status, mode := decode_command(41, SNAP_ACTION_SET_ENABLED, 1, bytes[:])
	testing.expect(t, status == CALIBER_STATUS_OK && mode == SNAP_MODE_NEAREST)
	testing.expect(t, encode_command(41, SNAP_ACTION_SET_ENABLED, 0, bytes[:]) == CALIBER_STATUS_OK)
	status, mode = decode_command(41, SNAP_ACTION_SET_ENABLED, 0, bytes[:])
	testing.expect(t, status == CALIBER_STATUS_OK && mode == SNAP_MODE_OFF)
	testing.expect(t, encode_command(41, SNAP_ACTION_SET_MODE, SNAP_MODE_PRIOR, bytes[:]) == CALIBER_STATUS_OK)
	status, mode = decode_command(41, SNAP_ACTION_SET_MODE, SNAP_MODE_PRIOR, bytes[:])
	testing.expect(t, status == CALIBER_STATUS_OK && mode == SNAP_MODE_PRIOR)
}

@(test)
test_snap_command_rejects_wrong_identity_and_corruption :: proc(t: ^testing.T) {
	bytes: [COMMAND_SIZE]u8
	testing.expect(t, encode_command(41, SNAP_ACTION_SET_MODE, SNAP_MODE_NEAREST, bytes[:]) == CALIBER_STATUS_OK)
	status, _ := decode_command(42, SNAP_ACTION_SET_MODE, SNAP_MODE_NEAREST, bytes[:])
	testing.expect(t, status == CALIBER_STATUS_INVALID_ARGUMENT)
	bytes[20] = 1
	status, _ = decode_command(41, SNAP_ACTION_SET_MODE, SNAP_MODE_NEAREST, bytes[:])
	testing.expect(t, status == CALIBER_STATUS_INVALID_ARGUMENT)
	testing.expect(t, encode_command(0, SNAP_ACTION_SET_MODE, SNAP_MODE_OFF, bytes[:]) == CALIBER_STATUS_INVALID_ARGUMENT)
	testing.expect(t, encode_command(41, SNAP_ACTION_SET_MODE, 3, bytes[:]) == CALIBER_STATUS_INVALID_ARGUMENT)
}
