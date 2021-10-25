use mold::{Composition, Fill, Func, Path, Point, Props, Style};
use rand::prelude::*;

use crate::cassia::{Cassia, Styling};

mod debugging {
    use super::*;

    use std::{fs::OpenOptions, io::Write};

    use mold::Buffer;

    pub fn capture_to_file(width: usize, height: usize, composition: &mut Composition) {
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

    pub fn segs_to_file(composition: &mut Composition) {
        let pixel_segments = composition.pixel_segments();

        let new_path = "circles.segs";
        let mut output = OpenOptions::new()
            .write(true)
            .create(true)
            .open(new_path)
            .unwrap();
        output.write(bytemuck::cast_slice(pixel_segments)).unwrap();
    }

    pub fn stylings_to_file(stylings: &[Styling]) {
        let new_path = "circles.stylings";
        let mut output = OpenOptions::new()
            .write(true)
            .create(true)
            .open(new_path)
            .unwrap();
        output.write(bytemuck::cast_slice(stylings)).unwrap();
    }
}

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

pub fn circles_main(count: usize) {
    let width = 1000;
    let height = 1000;
    let radius_range = 10.0..50.0;

    let mut composition = Composition::new();
    let mut stylings = Vec::new();
    let mut rng = StdRng::seed_from_u64(42);

    for _ in 0..count {
        let layer_id = composition.create_layer().unwrap();
        composition.insert_in_layer(
            layer_id,
            &circle(
                rng.gen_range(0.0..width as f32),
                rng.gen_range(0.0..height as f32),
                rng.gen_range(radius_range.clone()),
            ),
        );

        let color = [rng.gen(), rng.gen(), rng.gen(), 0.5];

        composition.get_mut(layer_id).unwrap().set_props(Props {
            func: Func::Draw(Style {
                fill: Fill::Solid(color),
                ..Default::default()
            }),
            ..Default::default()
        });

        stylings.push(Styling {
            fill: color,
            fill_rule: 0,
            blend_mode: 0,
            ..Default::default()
        });
    }

    let cassia = Cassia::new(width, height);
    cassia.render(composition.pixel_segments(), &stylings);

    std::thread::sleep(std::time::Duration::from_secs(10));

    // debugging::capture_to_file(width, height, &mut composition);
    // debugging::segs_to_file(&mut composition);
    // debugging::stylings_to_file(&stylings);
}
