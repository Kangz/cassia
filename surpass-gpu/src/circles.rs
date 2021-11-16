use std::mem;

use mold::{Composition, Path, Point};
use painter::{Color, CompactPixelSegment, Style};
use rand::prelude::*;

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

pub fn circles(count: usize) -> (Vec<CompactPixelSegment>, Vec<Style>) {
    let width = 1000;
    let height = 1000;
    let radius_range = 10.0..50.0;

    let mut composition = Composition::new();
    let mut styles = Vec::new();
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

        let color = [rng.gen(), rng.gen(), rng.gen(), 0.2];

        styles.push(Style {
            fill_rule: 0,
            color: Color {
                r: color[0],
                g: color[1],
                b: color[2],
                a: color[3],
            },
            blend_mode: 0,
        });
    }

    (
        unsafe { mem::transmute(composition.pixel_segments().to_owned()) },
        styles,
    )
}
