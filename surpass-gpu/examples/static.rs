use std::borrow::Cow;

use rand::prelude::*;
use painter::{
    Color, CompactPixelSegment, Instance, SegmentRange, TILE_HEIGHT, TILE_HEIGHT_SHIFT, TILE_WIDTH,
    TILE_WIDTH_SHIFT,
};
use winit::{
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoop},
    window::Window,
};

fn vertical_lines(
    height: u32,
    lines: u32,
) -> (Vec<CompactPixelSegment>, Vec<SegmentRange>, Vec<Color>) {
    let mut segments = Vec::new();
    let mut ranges = Vec::new();
    let mut styles = Vec::new();
    let mut small_rng = SmallRng::seed_from_u64(0);

    for tile_y in (0..height).step_by(TILE_HEIGHT) {
        let start = segments.len() as u32;

        for line in 0..lines {
            let x = line;

            for y in 0..TILE_HEIGHT {
                let y = y + tile_y as usize;

                segments.push(CompactPixelSegment::new(
                    0,
                    y as i32 >> TILE_HEIGHT_SHIFT,
                    x as i32 >> TILE_WIDTH_SHIFT,
                    x,
                    y as u32 & (TILE_HEIGHT - 1) as u32,
                    x as u32 & (TILE_WIDTH - 1) as u32,
                    256,
                    0,
                ));
            }
        }

        let end = segments.len() as u32;

        ranges.push(SegmentRange {
            start,
            negs: end,
            end,
        });
    }

    for _ in 0..lines {
        styles.push(Color {
            r: small_rng.gen(),
            g: small_rng.gen(),
            b: small_rng.gen(),
            a: 1.0,
        });
    }

    (segments, ranges, styles)
}

fn horizontal_lines(height: u32) -> (Vec<CompactPixelSegment>, Vec<SegmentRange>, Vec<Color>) {
    let mut segments = Vec::new();
    let mut ranges = Vec::new();
    let mut styles = Vec::new();
    let mut small_rng = SmallRng::seed_from_u64(0);

    for tile_y in (0..height).step_by(TILE_HEIGHT) {
        let start = segments.len() as u32;

        for y in 0..TILE_HEIGHT {
            let x = -10;
            let y = y + tile_y as usize;

            segments.push(CompactPixelSegment::new(
                0,
                y as i32 >> TILE_HEIGHT_SHIFT,
                x as i32 >> TILE_WIDTH_SHIFT,
                y as u32,
                y as u32 & (TILE_HEIGHT - 1) as u32,
                x as u32 & (TILE_WIDTH - 1) as u32,
                0,
                16,
            ));
        }

        let end = segments.len() as u32;

        ranges.push(SegmentRange {
            start,
            negs: start,
            end,
        });
    }

    for _ in 0..height {
        styles.push(Color {
            r: small_rng.gen(),
            g: small_rng.gen(),
            b: small_rng.gen(),
            a: 1.0,
        });
    }

    (segments, ranges, styles)
}

fn horizontal_stripes(
    height: u32,
    length: u32,
    overlaps: u32,
) -> (Vec<CompactPixelSegment>, Vec<SegmentRange>, Vec<Color>) {
    let mut segments = Vec::new();
    let mut ranges = Vec::new();
    let mut styles = Vec::new();
    let mut small_rng = SmallRng::seed_from_u64(0);

    for tile_y in (0..height).step_by(TILE_HEIGHT) {
        let start = segments.len() as u32;

        for i in 0..overlaps * 2 {
            for y in 0..TILE_HEIGHT {
                let x = i * length;
                let y = y + tile_y as usize;

                segments.push(CompactPixelSegment::new(
                    0,
                    y as i32 >> TILE_HEIGHT_SHIFT,
                    x as i32 >> TILE_WIDTH_SHIFT,
                    i % overlaps,
                    y as u32 & (TILE_HEIGHT - 1) as u32,
                    x as u32 & (TILE_WIDTH - 1) as u32,
                    0,
                    16 * if i < overlaps { 1 } else { -1 },
                ));
            }
        }

        let end = segments.len() as u32;

        ranges.push(SegmentRange {
            start,
            negs: end,
            end,
        });
    }

    for _ in 0..overlaps {
        styles.push(Color {
            r: small_rng.gen(),
            g: small_rng.gen(),
            b: small_rng.gen(),
            a: 0.3,
        });
    }

    (segments, ranges, styles)
}

async fn run(event_loop: EventLoop<()>, window: Window) {
    let (segments, ranges, styles) = vertical_lines(2048, 2048);
    // let (segments, ranges, styles) = horizontal_lines(2000);
    // let (segments, ranges, styles) = horizontal_stripes(2000, 200, 5);

    let mut current_size = window.inner_size();
    let instance = wgpu::Instance::new(wgpu::Backends::PRIMARY);
    let surface = unsafe { instance.create_surface(&window) };
    let adapter = instance
        .request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            ..Default::default()
        })
        .await
        .expect("failed to find an appropriate adapter");

    let (device, queue) = adapter
        .request_device(
            &wgpu::DeviceDescriptor {
                label: None,
                features: wgpu::Features::empty(),
                limits: wgpu::Limits {
                    max_texture_dimension_2d: 4096,
                    ..wgpu::Limits::downlevel_defaults()
                },
            },
            None,
        )
        .await
        .expect("failed to get device");

    let mut surpass_instance = Instance::new(&device, current_size.width, current_size.height)
        .await
        .expect("failed to create surpass Instance");

    let shader = device.create_shader_module(&wgpu::ShaderModuleDescriptor {
        label: None,
        source: wgpu::ShaderSource::Wgsl(Cow::Borrowed(include_str!("draw_texture.wgsl"))),
    });

    let swap_chain_format = surface.get_preferred_format(&adapter).unwrap();

    let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: None,
        layout: None,
        vertex: wgpu::VertexState {
            module: &shader,
            entry_point: "vs_main",
            buffers: &[],
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader,
            entry_point: "fs_main",
            targets: &[swap_chain_format.into()],
        }),
        primitive: wgpu::PrimitiveState::default(),
        depth_stencil: None,
        multisample: wgpu::MultisampleState::default(),
    });

    let sampler = device.create_sampler(&wgpu::SamplerDescriptor::default());

    let bind_group_layout = render_pipeline.get_bind_group_layout(0);

    let mut config = wgpu::SurfaceConfiguration {
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
        format: swap_chain_format,
        width: current_size.width,
        height: current_size.height,
        present_mode: wgpu::PresentMode::Mailbox,
    };

    surface.configure(&device, &config);

    event_loop.run(move |event, _, control_flow| {
        let _ = (&instance, &adapter, &shader);

        *control_flow = ControlFlow::Poll;
        window.request_redraw();

        match event {
            Event::WindowEvent {
                event: WindowEvent::Resized(size),
                ..
            } => {
                current_size = size;

                config.width = size.width;
                config.height = size.height;
                surface.configure(&device, &config);
            }
            Event::RedrawRequested(_) => {
                let frame = surface
                    .get_current_texture()
                    .expect("Failed to acquire next swap chain texture");
                let view = frame
                    .texture
                    .create_view(&wgpu::TextureViewDescriptor::default());

                let mut encoder =
                    device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: None });

                surpass_instance.encode_painting(
                    &device,
                    &mut encoder,
                    &segments,
                    &ranges,
                    &styles,
                    current_size.width,
                    current_size.height,
                    Color {
                        r: 0.78,
                        g: 0.12,
                        b: 0.56,
                        a: 1.0,
                    },
                );
                let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
                    layout: &bind_group_layout,
                    entries: &[
                        wgpu::BindGroupEntry {
                            binding: 0,
                            resource: wgpu::BindingResource::TextureView(
                                &surpass_instance.texture_view(),
                            ),
                        },
                        wgpu::BindGroupEntry {
                            binding: 1,
                            resource: wgpu::BindingResource::Sampler(&sampler),
                        },
                    ],
                    label: None,
                });

                {
                    let mut rpass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                        label: None,
                        color_attachments: &[wgpu::RenderPassColorAttachment {
                            view: &view,
                            resolve_target: None,
                            ops: wgpu::Operations {
                                load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                                store: true,
                            },
                        }],
                        depth_stencil_attachment: None,
                    });
                    rpass.set_pipeline(&render_pipeline);
                    rpass.set_bind_group(0, &bind_group, &[]);
                    rpass.draw(0..3, 0..1);
                }

                queue.submit(Some(encoder.finish()));
                frame.present();
            }
            Event::WindowEvent {
                event: WindowEvent::CloseRequested,
                ..
            } => *control_flow = ControlFlow::Exit,
            _ => {}
        }
    });
}

fn main() {
    let event_loop = EventLoop::new();
    let window = winit::window::Window::new(&event_loop).unwrap();

    env_logger::init();
    pollster::block_on(run(event_loop, window));
}
