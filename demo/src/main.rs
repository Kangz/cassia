use std::{fs::OpenOptions, io::Write};

use dlopen::wrapper::{Container, WrapperApi};
use dlopen_derive::WrapperApi;
use mold::{Buffer, Composition, Fill, Func, Path, Point, Props, Style};
use rand::{prelude::StdRng, Rng, SeedableRng};

fn circle(x: f32, y: f32, radius: f32) -> Path {
    let weight = 2.0f32.sqrt() / 2.0;

    let mut path = Path::new();

    path.rat_quad(
        (Point::new(x + radius, y), 1.0),
        (Point::new(x + radius, y - radius), weight),
        (Point::new(x, y - radius), 1.0),
    );
    path.rat_quad(
        (Point::new(x, y - radius), 1.0),
        (Point::new(x - radius, y - radius), weight),
        (Point::new(x - radius, y), 1.0),
    );
    path.rat_quad(
        (Point::new(x - radius, y), 1.0),
        (Point::new(x - radius, y + radius), weight),
        (Point::new(x, y + radius), 1.0),
    );
    path.rat_quad(
        (Point::new(x, y + radius), 1.0),
        (Point::new(x + radius, y + radius), weight),
        (Point::new(x + radius, y), 1.0),
    );

    path
}

fn capture_to_file(width: usize, height: usize, composition: &mut Composition) {
    let mut buffer = vec![[255u8; 4]; width * height];

    composition.render(
        Buffer {
            buffer: &mut buffer,
            width,
            ..Default::default()
        },
        [1.0, 1.0, 1.0, 1.0],
        None,
    );

    let mut bytes = Vec::with_capacity(width * height * 3);
    for [r, g, b, _] in &buffer {
        bytes.push(*r);
        bytes.push(*g);
        bytes.push(*b);
    }
    let new_path = "capture.ppm";
    let mut output = OpenOptions::new()
        .write(true)
        .create(true)
        .open(new_path)
        .unwrap();
    output
        .write(format!("P6\n{} {}\n255\n", width, height).as_bytes())
        .unwrap();
    output.write(&bytes).unwrap();
}

#[derive(WrapperApi)]
struct Cassia {
    cassia_init: unsafe extern "C" fn(width: u32, height: u32),
    cassia_render: unsafe extern "C" fn(pixel_segments: *const u64, len: usize),
    cassia_shutdown: unsafe extern "C" fn(),
}

fn render_with_cassia(width: usize, height: usize, composition: &mut Composition) {
    let pixel_segments = composition.pixel_segments();

    let mut cont: Container<Cassia> =
        unsafe { Container::load("../out/libcassia.so") }.unwrap();

    unsafe {
        cont.cassia_init(width as u32, height as u32);
        cont.cassia_render(pixel_segments.as_ptr() as *const _, pixel_segments.len());
        cont.cassia_shutdown();
    }
}

fn main() {
    let width = 1000;
    let height = 1000;
    let circles = 100;
    let radius_range = 10.0..50.0;

    let mut composition = Composition::new();
    let mut rng = StdRng::seed_from_u64(42);

    for _ in 0..circles {
        let layer_id = composition.create_layer().unwrap();
        composition.insert_in_layer(
            layer_id,
            &circle(
                rng.gen_range(0.0..width as f32),
                rng.gen_range(0.0..height as f32),
                rng.gen_range(radius_range.clone()),
            ),
        );

        composition.get_mut(layer_id).unwrap().set_props(Props {
            func: Func::Draw(Style {
                fill: Fill::Solid([
                    rng.gen(),
                    rng.gen(),
                    rng.gen(),
                    1.0,
                ]),
                ..Default::default()
            }),
            ..Default::default()
        });
    }

    // capture_to_file(width, height, &mut composition);
    render_with_cassia(width, height, &mut composition);
}
