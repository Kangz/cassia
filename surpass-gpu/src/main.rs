use std::{borrow::Cow, fmt, mem, path::PathBuf, str::FromStr, time::Duration};

use painter::{Color, CompactPixelSegment, Instance, Style};
use structopt::StructOpt;
use winit::{
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoop},
    window::Window,
};

mod circles;
mod lines;
mod svg;

#[derive(StructOpt)]
#[structopt(about = "painter demo with multiple modes")]
struct Demo {
    #[structopt(subcommand)]
    mode: Mode,
    /// Use high-performance GPU if available
    #[structopt(short, long)]
    high_performance: bool,
}

enum LinesMode {
    Vertical,
    Horizontal,
}

impl fmt::Display for LinesMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LinesMode::Vertical => write!(f, "vertical"),
            LinesMode::Horizontal => write!(f, "horizontal"),
        }
    }
}

impl FromStr for LinesMode {
    type Err = &'static str;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "v" | "vert" | "vertical" => Ok(LinesMode::Vertical),
            "h" | "hor" | "horizontal" => Ok(LinesMode::Horizontal),
            _ => Err("must be vertical or horizontal"),
        }
    }
}

#[derive(StructOpt)]
enum Mode {
    /// Renders random circles
    Lines {
        /// Either vertical or horizontal
        #[structopt(default_value = "vertical")]
        mode: LinesMode,
    },
    /// Renders random circles
    Circles {
        /// Amount of circles to draw
        #[structopt(default_value = "100")]
        count: usize,
    },
    /// Renders an SVG
    Svg {
        /// .svg input file
        #[structopt(parse(from_os_str))]
        file: PathBuf,
        /// Scale of the SVG
        #[structopt(short, long, default_value = "1.0")]
        scale: f32,
    },
}

async fn run(
    event_loop: EventLoop<()>,
    window: Window,
    segments: Vec<CompactPixelSegment>,
    styles: Vec<Style>,
    power_preference: wgpu::PowerPreference,
) {
    window.set_title("demo | painter time: ???ms");

    let mut painter_durations = Vec::new();

    let mut current_size = window.inner_size();
    let instance = wgpu::Instance::new(wgpu::Backends::PRIMARY);
    let surface = unsafe { instance.create_surface(&window) };
    let adapter = instance
        .request_adapter(&wgpu::RequestAdapterOptions {
            power_preference,
            ..Default::default()
        })
        .await
        .expect("failed to find an appropriate adapter");

    let adapter_features = adapter.features();
    let has_timestamp_query = adapter_features.contains(wgpu::Features::TIMESTAMP_QUERY);

    let (device, queue) = adapter
        .request_device(
            &wgpu::DeviceDescriptor {
                label: None,
                features: wgpu::Features::TIMESTAMP_QUERY & adapter_features,
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
                if painter_durations.len() == 50 {
                    let min = painter_durations
                        .iter()
                        .min_by(|a: &&f64, b| a.partial_cmp(b).unwrap())
                        .copied()
                        .unwrap();
                    let max = painter_durations
                        .iter()
                        .max_by(|a, b| a.partial_cmp(b).unwrap())
                        .copied()
                        .unwrap();

                    window.set_title(&format!(
                        "demo | painter time: {:.2}ms (min: {:.2}ms, max: {:.2}ms)",
                        painter_durations.drain(..).sum::<f64>() / 50.0,
                        min,
                        max,
                    ));
                }

                let frame = surface
                    .get_current_texture()
                    .expect("Failed to acquire next swap chain texture");
                let view = frame
                    .texture
                    .create_view(&wgpu::TextureViewDescriptor::default());

                let timestamp_context = has_timestamp_query.then(|| {
                    let timestamp = device.create_query_set(&wgpu::QuerySetDescriptor {
                        label: None,
                        count: 2,
                        ty: wgpu::QueryType::Timestamp,
                    });

                    let timestamp_period = queue.get_timestamp_period();

                    let data_buffer = device.create_buffer(&wgpu::BufferDescriptor {
                        label: None,
                        size: 2 * mem::size_of::<u64>() as wgpu::BufferAddress,
                        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                        mapped_at_creation: false,
                    });

                    (timestamp, timestamp_period, data_buffer)
                });

                let mut encoder =
                    device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: None });

                surpass_instance.encode_painting(
                    &device,
                    &mut encoder,
                    &segments,
                    &styles,
                    current_size.width,
                    current_size.height,
                    Color {
                        // r: 0.05,
                        // g: 0.05,
                        // b: 0.07,
                        // a: 1.0,
                        r: 1.0,
                        g: 1.0,
                        b: 1.0,
                        a: 1.0,
                    },
                    timestamp_context
                        .as_ref()
                        .map(|(timestamp, _, _)| timestamp),
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

                if let Some((timestamp, _, data_buffer)) = &timestamp_context {
                    encoder.resolve_query_set(&timestamp, 0..2, &data_buffer, 0);
                }

                queue.submit(Some(encoder.finish()));
                frame.present();

                if let Some((_, timestamp_period, data_buffer)) = timestamp_context.as_ref() {
                    use bytemuck::{Pod, Zeroable};

                    #[repr(C)]
                    #[derive(Clone, Copy, Debug, Pod, Zeroable)]
                    struct TimestampData {
                        start: u64,
                        end: u64,
                    }

                    let _ = data_buffer.slice(..).map_async(wgpu::MapMode::Read);

                    device.poll(wgpu::Maintain::Wait);

                    let view = data_buffer.slice(..).get_mapped_range();
                    let timestamp: &TimestampData = bytemuck::from_bytes(&*view);
                    let nanos = (timestamp.end - timestamp.start) as f32 * timestamp_period;

                    painter_durations
                        .push(Duration::from_nanos(nanos as u64).as_secs_f64() * 1000.0);
                }
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
    let mut opts = Demo::from_args();

    let (segments, styles) = match opts.mode {
        Mode::Lines {
            mode: LinesMode::Vertical,
        } => lines::vertical_lines(2048, 2048),
        Mode::Lines {
            mode: LinesMode::Horizontal,
        } => lines::horizontal_lines(2048),
        Mode::Circles { count } => circles::circles(count),
        Mode::Svg { file, scale } => svg::svg(&file, scale),
    };
    let power_preference = if opts.high_performance {
        wgpu::PowerPreference::HighPerformance
    } else {
        wgpu::PowerPreference::LowPower
    };

    let event_loop = EventLoop::new();
    let window = winit::window::Window::new(&event_loop).unwrap();

    env_logger::init();
    pollster::block_on(run(event_loop, window, segments, styles, power_preference));
}
