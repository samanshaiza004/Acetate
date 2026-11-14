package audacity_caliber_snap

import "core:testing"

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
