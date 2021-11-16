use painter::{Color, CompactPixelSegment, Style, TILE_HEIGHT};

use rand::prelude::*;

pub fn vertical_lines(height: u32, lines: u32) -> (Vec<CompactPixelSegment>, Vec<Style>) {
    let mut segments = Vec::new();
    let mut styles = Vec::new();
    let mut small_rng = SmallRng::seed_from_u64(0);

    for tile_y in (0..height).step_by(TILE_HEIGHT) {
        for line in 0..lines {
            let x = line;

            for y in 0..TILE_HEIGHT {
                let y = y + tile_y as usize;

                segments.push(CompactPixelSegment::new_xy(x as i32, y as i32, x, 256, 0));
            }
        }
    }

    for _ in 0..lines {
        styles.push(Style {
            fill_rule: 0,
            color: Color {
                r: small_rng.gen(),
                g: small_rng.gen(),
                b: small_rng.gen(),
                a: 1.0,
            },
            blend_mode: 0,
        });
    }

    (segments, styles)
}

pub fn horizontal_lines(height: u32) -> (Vec<CompactPixelSegment>, Vec<Style>) {
    let mut segments = Vec::new();
    let mut styles = Vec::new();
    let mut small_rng = SmallRng::seed_from_u64(0);

    for tile_y in (0..height).step_by(TILE_HEIGHT) {
        for y in 0..TILE_HEIGHT {
            let x = -100;
            let y = y + tile_y as usize;

            segments.push(CompactPixelSegment::new_xy(x, y as i32, y as u32, 0, 16));
        }
    }

    for _ in 0..height {
        styles.push(Style {
            fill_rule: 0,
            color: Color {
                r: small_rng.gen(),
                g: small_rng.gen(),
                b: small_rng.gen(),
                a: 1.0,
            },
            blend_mode: 0,
        });
    }

    (segments, styles)
}
