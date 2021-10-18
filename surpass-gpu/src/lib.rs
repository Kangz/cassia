use std::{borrow::Cow, mem, slice};

pub use wgpu;
use wgpu::util::DeviceExt;

mod compact_pixel_segment;

pub use compact_pixel_segment::CompactPixelSegment;

pub const TILE_WIDTH: usize = 8;
pub const TILE_WIDTH_SHIFT: u32 = TILE_WIDTH.trailing_zeros();
pub const TILE_HEIGHT: usize = 8;
pub const TILE_HEIGHT_SHIFT: u32 = TILE_HEIGHT.trailing_zeros();

#[derive(Clone, Copy)]
#[repr(C)]
pub struct Color {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

#[derive(Copy, Clone)]
#[repr(C)]
pub struct Config {
    pub width: u32,
    pub height: u32,
    pub clear_color: Color,
}

#[cfg(not(target_arch = "spirv"))]
impl Config {
    pub fn as_byte_slice(&self) -> &[u8] {
        unsafe { slice::from_raw_parts((self as *const _) as *const u8, mem::size_of::<Self>()) }
    }
}

#[derive(Copy, Clone)]
#[repr(C)]
pub struct SegmentRange {
    pub start: u32,
    pub negs: u32,
    pub end: u32,
}

unsafe fn as_byte_slice<T: Sized>(slice: &[T]) -> &[u8] {
    slice::from_raw_parts(
        slice.as_ptr() as *const u8,
        slice.len() * mem::size_of::<T>(),
    )
}

pub struct Instance {
    texture: wgpu::Texture,
    width: u32,
    height: u32,
    bind_group_layout: wgpu::BindGroupLayout,
    compute_pipeline: wgpu::ComputePipeline,
}

impl Instance {
    pub async fn new(device: &wgpu::Device, width: u32, height: u32) -> Option<Self> {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: None,
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba16Float,
            usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING,
        });

        // let module = unsafe { device.create_shader_module_spirv(&wgpu::include_spirv_raw!("paint.spv")) };
        let module = device.create_shader_module(&wgpu::ShaderModuleDescriptor {
            label: None,
            source: wgpu::ShaderSource::Wgsl(Cow::Borrowed(include_str!("hardcoded.wgsl"))),
        });

        let compute_pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: None,
            layout: None,
            module: &module,
            entry_point: "paint",
        });

        let bind_group_layout = compute_pipeline.get_bind_group_layout(0);

        Some(Self {
            texture,
            width,
            height,
            bind_group_layout,
            compute_pipeline,
        })
    }

    pub fn texture_view(&self) -> wgpu::TextureView {
        self.texture
            .create_view(&wgpu::TextureViewDescriptor::default())
    }

    pub fn encode_painting(
        &mut self,
        device: &wgpu::Device,
        encoder: &mut wgpu::CommandEncoder,
        segments: &[CompactPixelSegment],
        ranges: &[SegmentRange],
        styles: &[Color],
        width: u32,
        height: u32,
        clear_color: Color,
    ) {
        if segments.is_empty() || width == 0 || height == 0 {
            return;
        }

        if width != self.width || height != self.height {
            self.texture.destroy();
            self.texture = device.create_texture(&wgpu::TextureDescriptor {
                label: None,
                size: wgpu::Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Rgba16Float,
                usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING,
            });
        }

        let config = Config {
            width,
            height,
            clear_color,
        };

        let config_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: None,
            contents: config.as_byte_slice(),
            usage: wgpu::BufferUsages::UNIFORM,
        });
        let segments_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: None,
            contents: unsafe { as_byte_slice(segments) },
            usage: wgpu::BufferUsages::STORAGE,
        });
        let ranges_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: None,
            contents: unsafe { as_byte_slice(ranges) },
            usage: wgpu::BufferUsages::STORAGE,
        });
        let styles_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: None,
            contents: unsafe { as_byte_slice(styles) },
            usage: wgpu::BufferUsages::STORAGE,
        });

        let texture_view = self
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());

        let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: None,
            layout: &self.bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: config_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: segments_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: styles_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: wgpu::BindingResource::TextureView(&texture_view),
                },
            ],
        });

        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor { label: None });
        pass.set_pipeline(&self.compute_pipeline);
        pass.set_bind_group(0, &bind_group, &[]);
        pass.dispatch(
            height / TILE_HEIGHT as u32
                + (height % TILE_HEIGHT as u32 != 1)
                    .then(|| 1)
                    .unwrap_or_default(),
            1,
            1,
        );
    }
}
