use bytemuck::{Pod, Zeroable};
use dlopen::wrapper::{Container, WrapperApi};
use dlopen_derive::WrapperApi;
use mold::CompactSegment;

#[repr(C)]
#[derive(Clone, Copy, Default, Pod, Zeroable)]
pub struct Styling {
    pub fill: [f32; 4],
    pub fill_rule: u32,
    pub blend_mode: u32,
    pub _padding: [u32; 2],
}

#[derive(WrapperApi)]
struct CassiaSys {
    cassia_init: unsafe extern "C" fn(width: u32, height: u32),
    cassia_render: unsafe extern "C" fn(
        pixel_segments: *const u64,
        pixel_segments_len: usize,
        stylings: *const Styling,
        stylings_len: usize,
    ),
    cassia_shutdown: unsafe extern "C" fn(),
}

pub struct Cassia {
    cont: Container<CassiaSys>,
}

impl Cassia {
    pub fn new(width: usize, height: usize) -> Self {
        #[cfg(target_os="macos")]
        const CASSIA_LIB_NAME : &str = "../out/libcassia.dylib";
        #[cfg(target_os="linux")]
        const CASSIA_LIB_NAME : &str = "../out/libcassia.so";

        let cont: Container<CassiaSys> = unsafe { Container::load(CASSIA_LIB_NAME) }.unwrap();

        unsafe { cont.cassia_init(width as u32, height as u32); }

        Self {
            cont,
        }
    }

    pub fn render(&self, pixel_segments: &[CompactSegment], stylings: &[Styling]) {
        unsafe {
            self.cont.cassia_render(
                pixel_segments.as_ptr() as *const _,
                pixel_segments.len(),
                stylings.as_ptr(),
                stylings.len(),
            );
        }
    }
}

impl Drop for Cassia {
    fn drop(&mut self) {
        unsafe { self.cont.cassia_shutdown(); }
    }
}