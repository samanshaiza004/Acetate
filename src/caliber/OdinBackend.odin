package audacity_backend

import "core:testing"

// Mirrors lib-utility/MathApprox.h's FastLog2 bit approximation. Keeping this
// approximation here preserves the compressor's existing response, including
// its behavior for zero and subnormal input.
odin_fast_log2 :: proc "contextless" (x: f32) -> f32 {
	bits := transmute(u32) x
	log_2 := f32(int((bits >> 23) & 255) - 128)
	normalized_bits := (bits & ~(u32(255) << 23)) + (u32(127) << 23)
	normalized := transmute(f32) normalized_bits
	return log_2 + ((-0.3358287811 * normalized + 2.0) * normalized - 0.65871759316667)
}

odin_abs_f32 :: proc "contextless" (x: f32) -> f32 {
	bits := transmute(u32) x
	return transmute(f32) (bits & 0x7fffffff)
}

odin_compressor_characteristic :: proc "contextless" (
	over_shoot: f32,
	knee: f32,
	slope: f32,
) -> f32 {
	knee_half := knee / 2.0
	if over_shoot <= -knee_half {
		return 0.0
	} else if over_shoot > -knee_half && over_shoot <= knee_half {
		return 0.5 * slope * (over_shoot + knee_half) * (over_shoot + knee_half) / knee
	} else {
		return slope * over_shoot
	}
}

@(export, link_name="audacity_odin_compressor_process_envelope")
odin_compressor_process_envelope :: proc "c" (
	sidechain: [^]f32,
	destination: [^]f32,
	block_len: uintptr,
	threshold_db: f32,
	knee_db: f32,
	slope: f32,
	alpha_attack: f32,
	alpha_release: f32,
	state: ^f32,
	max_input_db: ^f32,
	max_gain_reduction_db: ^f32,
) {
	if state == nil || max_input_db == nil || max_gain_reduction_db == nil do return

	negative_infinity := transmute(f32) u32(0xff800000)
	max_input: f32 = negative_infinity
	max_gain_reduction: f32 = 0.0
	if block_len > 0 && sidechain != nil && destination != nil {
		log2_to_db: f32 = 20.0 / 3.321928094887362
		for i in 0..<block_len {
			level_db := log2_to_db * odin_fast_log2(odin_abs_f32(sidechain[i]))
			if level_db > max_input {
				max_input = level_db
			}

			over_shoot := level_db - threshold_db
			gain_reduction := odin_compressor_characteristic(over_shoot, knee_db, slope)
			diff := gain_reduction - state^
			if diff < 0.0 {
				state^ += alpha_attack * diff
			} else {
				state^ += alpha_release * diff
			}
			destination[i] = state^
			if state^ < max_gain_reduction {
				max_gain_reduction = state^
			}
		}
	}

	max_input_db^ = max_input
	max_gain_reduction_db^ = max_gain_reduction
}

// Mirrors LookAheadGainReduction::process. The C++ processor owns the ring
// buffer and its lifetime; Odin only shapes the borrowed buffer in place.
@(export, link_name="audacity_odin_compressor_lookahead_process")
odin_compressor_lookahead_process :: proc "c" (
	buffer: [^]f32,
	buffer_size: i32,
	write_position: i32,
	last_pushed_samples: i32,
	delay_in_samples: i32,
) {
	if buffer == nil || buffer_size <= 0 do return

	next_gain_reduction_value: f32 = 0.0
	step: f32 = 0.0

	index := write_position - 1
	if index < 0 do index += buffer_size

	size_1 := index + 1
	if last_pushed_samples < size_1 do size_1 = last_pushed_samples
	if last_pushed_samples <= 0 do size_1 = 0
	size_2 := last_pushed_samples - size_1

	for _ in 0..<size_1 {
		sample := buffer[index]
		if sample > next_gain_reduction_value {
			buffer[index] = next_gain_reduction_value
			next_gain_reduction_value += step
		} else {
			step = -sample / f32(delay_in_samples)
			next_gain_reduction_value = sample + step
		}
		index -= 1
	}

	if size_2 > 0 {
		index = buffer_size - 1
		for _ in 0..<size_2 {
			sample := buffer[index]
			if sample > next_gain_reduction_value {
				buffer[index] = next_gain_reduction_value
				next_gain_reduction_value += step
			} else {
				step = -sample / f32(delay_in_samples)
				next_gain_reduction_value = sample + step
			}
			index -= 1
		}
	}

	if index < 0 do index = buffer_size - 1

	size_1 = index + 1
	if delay_in_samples < size_1 do size_1 = delay_in_samples
	if delay_in_samples <= 0 do size_1 = 0
	size_2 = delay_in_samples - size_1
	break_was_used := false

	for _ in 0..<size_1 {
		sample := buffer[index]
		if sample > next_gain_reduction_value {
			buffer[index] = next_gain_reduction_value
			next_gain_reduction_value += step
		} else {
			break_was_used = true
			break
		}
		index -= 1
	}

	if !break_was_used && size_2 > 0 {
		index = buffer_size - 1
		for _ in 0..<size_2 {
			sample := buffer[index]
			if sample > next_gain_reduction_value {
				buffer[index] = next_gain_reduction_value
				next_gain_reduction_value += step
			} else {
				break
			}
			index -= 1
		}
	}
}

@(export, link_name="audacity_odin_fade_process_block")
odin_fade_process_block :: proc "c" (
	fade_in: u8,
	total_samples: i64,
	sample_index: ^i64,
	input: [^]f32,
	output: [^]f32,
	block_len: uintptr,
) -> uintptr {
	if sample_index == nil do return 0
	if block_len == 0 do return 0
	if input == nil || output == nil do return 0
	if total_samples <= 0 do return 0

	for i in 0..<block_len {
		index := sample_index^
		weight_index := index
		if fade_in == 0 {
			weight_index = total_samples - 1 - index
		}
		output[i] = (input[i] * f32(weight_index)) / f32(total_samples)
		sample_index^ = index + 1
	}
	return block_len
}

@(export, link_name="audacity_odin_normalize_accumulate_sum")
odin_normalize_accumulate_sum :: proc "c" (
	input: [^]f32,
	block_len: uintptr,
	initial_sum: f64,
) -> f64 {
	if block_len == 0 || input == nil do return initial_sum

	sum := initial_sum
	for i in 0..<block_len {
		sum += f64(input[i])
	}
	return sum
}

@(export, link_name="audacity_odin_normalize_process_block")
odin_normalize_process_block :: proc "c" (
	samples: [^]f32,
	block_len: uintptr,
	offset: f32,
	multiplier: f32,
) {
	if block_len == 0 || samples == nil do return

	for i in 0..<block_len {
		adjusted := (samples[i] + offset) * multiplier
		samples[i] = adjusted
	}
}

@(export, link_name="audacity_odin_zero_crossing_score_channel")
odin_zero_crossing_score_channel :: proc "c" (
	samples: [^]f32,
	sample_count: uintptr,
	candidate_scores: [^]f32,
	candidate_count: uintptr,
) {
	if samples == nil || candidate_scores == nil ||
		sample_count == 0 || candidate_count == 0 {
		return
	}

	previous: f64 = 2.0
	for i in 0..<sample_count {
		sample := samples[i]
		score := sample
		if score < 0 do score = -score

		if previous * f64(sample) > 0 {
			score = f32(f64(score) + 0.4)
		} else if previous > 0 {
			score = f32(f64(score) + 0.1)
		}
		previous = f64(sample)
		samples[i] = score
	}

	for i in 0..<candidate_count {
		j := i
		if candidate_count != sample_count {
			j = i * (sample_count - 1) / (candidate_count - 1)
		}
		candidate_scores[i] += samples[j]

		distance := int(i) - int(candidate_count / 2)
		if distance < 0 do distance = -distance
		penalty := 0.1 * f64(distance) / f64(candidate_count / 2)
		candidate_scores[i] = f32(f64(candidate_scores[i]) + penalty)
	}
}

@(export, link_name="audacity_odin_zero_crossing_best_offset")
odin_zero_crossing_best_offset :: proc "c" (
	candidate_scores: [^]f32,
	candidate_count: uintptr,
	track_count: i32,
	sample_offset: ^i64,
) -> u8 {
	if candidate_scores == nil || candidate_count == 0 || sample_offset == nil {
		return 0
	}

	argmin := 0
	minimum: f32 = 3.0
	for i in 0..<candidate_count {
		if candidate_scores[i] < minimum {
			argmin = int(i)
			minimum = candidate_scores[i]
		}
	}

	if track_count == 1 && f64(minimum) > 0.2 * f64(track_count) {
		return 0
	}
	if track_count > 1 && f64(minimum) > 0.6 * f64(track_count) {
		return 0
	}

	sample_offset^ = i64(argmin - int(candidate_count) / 2)
	return 1
}

@(test)
test_fade_in_and_out_weights :: proc(t: ^testing.T) {
	input := [4]f32{1, 1, 1, 1}
	fade_in: [4]f32
	fade_out: [4]f32
	in_index: i64
	out_index: i64

	processed := odin_fade_process_block(1, 4, &in_index,
		raw_data(input[:]), raw_data(fade_in[:]), 4)
	testing.expect(t, processed == 4)
	testing.expect(t, fade_in == [4]f32{0, 0.25, 0.5, 0.75})
	testing.expect(t, in_index == 4)

	processed = odin_fade_process_block(0, 4, &out_index,
		raw_data(input[:]), raw_data(fade_out[:]), 4)
	testing.expect(t, processed == 4)
	testing.expect(t, fade_out == [4]f32{0.75, 0.5, 0.25, 0})
	testing.expect(t, out_index == 4)
}

@(test)
test_fade_chunk_continuity_and_in_place_buffer :: proc(t: ^testing.T) {
	first := [2]f32{2, 2}
	second := [2]f32{2, 2}
	first_out: [2]f32
	second_out: [2]f32
	index: i64

	first_count := odin_fade_process_block(1, 4, &index,
		raw_data(first[:]), raw_data(first_out[:]), 2)
	second_count := odin_fade_process_block(1, 4, &index,
		raw_data(second[:]), raw_data(second_out[:]), 2)
	testing.expect(t, first_count == 2 && second_count == 2)
	testing.expect(t, first_out == [2]f32{0, 0.5})
	testing.expect(t, second_out == [2]f32{1, 1.5})
	testing.expect(t, index == 4)

	in_place := [3]f32{3, 3, 3}
	in_place_index: i64
	processed := odin_fade_process_block(1, 3, &in_place_index,
		raw_data(in_place[:]), raw_data(in_place[:]), 3)
	testing.expect(t, processed == 3)
	testing.expect(t, in_place == [3]f32{0, 1, 2})
}

@(test)
test_fade_rejects_missing_buffers_without_advancing :: proc(t: ^testing.T) {
	index: i64 = 7
	processed := odin_fade_process_block(1, 12, &index, nil, nil, 1)
	testing.expect(t, processed == 0)
	testing.expect(t, index == 7)

	processed = odin_fade_process_block(1, 12, &index, nil, nil, 0)
	testing.expect(t, processed == 0)
	testing.expect(t, index == 7)
}

@(test)
test_normalize_streaming_sum_and_sample_processing :: proc(t: ^testing.T) {
	first := [3]f32{-1, 0.5, 1}
	second := [2]f32{0.25, -0.75}
	sum := odin_normalize_accumulate_sum(raw_data(first[:]), 3, 2.0)
	sum = odin_normalize_accumulate_sum(raw_data(second[:]), 2, sum)
	testing.expect(t, sum == 2.0)

	samples := [4]f32{-1, -0.5, 0, 0.5}
	odin_normalize_process_block(raw_data(samples[:]), 4, 0.25, 2)
	testing.expect(t, samples == [4]f32{-1.5, -0.5, 0.5, 1.5})
}

@(test)
test_normalize_empty_blocks_are_noops :: proc(t: ^testing.T) {
	sum := odin_normalize_accumulate_sum(nil, 0, 3.5)
	testing.expect(t, sum == 3.5)
	odin_normalize_process_block(nil, 0, 1, 2)
}

@(test)
test_compressor_envelope_attack_release_and_in_place :: proc(t: ^testing.T) {
	samples := [8]f32{0.0, 0.01, 0.1, 0.5, 1.0, 0.5, 0.1, 0.0}
	state: f32
	max_input: f32
	max_reduction: f32
	odin_compressor_process_envelope(
		raw_data(samples[:]), raw_data(samples[:]), uintptr(len(samples)), -20.0, 6.0, -0.75,
		0.35, 0.1, &state, &max_input, &max_reduction,
	)
	testing.expect(t, samples[0] == 0.0)
	testing.expect(t, samples[4] < samples[3])
	testing.expect(t, samples[7] > samples[6])
	testing.expect(t, state == samples[7])
	testing.expect(t, max_input > -1.0 && max_input < 1.0)
	testing.expect(t, max_reduction == samples[5])
}

@(test)
test_compressor_empty_block_resets_meters_but_preserves_state :: proc(t: ^testing.T) {
	state: f32 = -3.0
	max_input: f32 = 1.0
	max_reduction: f32 = -1.0
	odin_compressor_process_envelope(
		nil, nil, 0, -20.0, 0.0, -0.5, 0.25, 0.1,
		&state, &max_input, &max_reduction,
	)
	testing.expect(t, state == -3.0)
	testing.expect(t, max_input < -1.0e30)
	testing.expect(t, max_reduction == 0.0)
}

@(test)
test_zero_crossing_scoring_and_threshold :: proc(t: ^testing.T) {
	samples := [4]f32{-1, -0.4, 0.2, 0.8}
	scores := [4]f32{}
	odin_zero_crossing_score_channel(raw_data(samples[:]), 4,
		raw_data(scores[:]), 4)
	testing.expect(t, scores == [4]f32{1.2, 0.85, 0.2, 1.25})

	offset: i64
	found := odin_zero_crossing_best_offset(raw_data(scores[:]), 4, 2, &offset)
	testing.expect(t, found == 1)
	testing.expect(t, offset == 0)
	found = odin_zero_crossing_best_offset(raw_data(scores[:]), 4, 1, &offset)
	testing.expect(t, found == 0)

	bad_scores := [2]f32{1, 1}
	found = odin_zero_crossing_best_offset(raw_data(bad_scores[:]), 2, 1, &offset)
	testing.expect(t, found == 0)
}
