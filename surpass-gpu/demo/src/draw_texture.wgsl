struct VertexOutput {
    [[builtin(position)]] pos: vec4<f32>;
    [[location(0)]] uv: vec2<f32>;
};

[[stage(vertex)]]
fn vs_main([[builtin(vertex_index)]] vertex_index: u32) -> VertexOutput {
    var output: VertexOutput;

    output.uv = vec2<f32>(
        f32((i32(vertex_index) << 1u) & 2),
        f32(i32(vertex_index) & 2),
    );

    let pos: vec2<f32> = 2.0 * output.uv - vec2<f32>(1.0, 1.0);
    output.pos = vec4<f32>(pos.x, pos.y, 0.0, 1.0);

    output.uv.y = 1.0 - output.uv.y;

    return output;
}

[[group(0), binding(0)]]
var texture: texture_2d<f32>;
[[group(0), binding(1)]]
var sampler: sampler;

[[stage(fragment)]]
fn fs_main([[location(0)]] uv: vec2<f32>) -> [[location(0)]] vec4<f32> {
    return textureSample(texture, sampler, uv);
}
