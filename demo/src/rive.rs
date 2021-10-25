use std::path;

use std::{collections::HashMap, fs, mem, num::NonZeroU64, slice, time::Instant};

use mold::{
    BlendMode, Buffer, Composition, Fill, Gradient, GradientBuilder, GradientType, Path, Point,
};
use rive::{
    animation::{LinearAnimation, LinearAnimationInstance},
    layout::{self, Alignment, Fit},
    math::{self, Aabb, Mat},
    shapes::{paint::Color32, Command, CommandPath},
    BinaryReader, File, PaintColor, RenderPaint, Renderer, Style,
};

use crate::cassia::{Cassia, Styling};

fn to_linear(color: Color32) -> [f32; 4] {
    fn conv(l: u8) -> f32 {
        let l = l as f32 * 255.0f32.recip();

        if l <= 0.04045 {
            l * 12.92f32.recip()
        } else {
            ((l + 0.055) * 1.055f32.recip()).powf(2.4)
        }
    }

    [
        conv(color.blue()),
        conv(color.green()),
        conv(color.red()),
        color.alpha() as f32 * 255.0f32.recip(),
    ]
}
struct PrintRenderer;

impl Renderer for PrintRenderer {
    fn draw(&mut self, path: &CommandPath, transform: Mat, paint: &RenderPaint) {
        println!("commands: {}", path.commands.len());
    }
}

#[derive(Clone, Debug)]
struct CachedLayer {
    layer_id: mold::LayerId,
    inverted_transform: Mat,
    was_used: bool,
}

struct MoldRenderer {
    cassia: Cassia,
    composition: Composition,
    cached_layers: HashMap<NonZeroU64, CachedLayer>,
    index: u16,
    tag: NonZeroU64,
}

impl MoldRenderer {
    pub fn new(width: usize, height: usize) -> Self {
        Self {
            cassia: Cassia::new(width, height),
            composition: Composition::new(),
            cached_layers: HashMap::new(),
            index: 0,
            tag: NonZeroU64::new(1).unwrap(),
        }
    }

    pub fn tag(&mut self) -> NonZeroU64 {
        let tag = self.tag;
        self.tag = NonZeroU64::new(tag.get().checked_add(1).unwrap_or(1)).unwrap();
        tag
    }

    pub fn reset(&mut self) {
        let cached_layers = self
            .cached_layers
            .drain()
            .filter(|(_, cached_layer)| !cached_layer.was_used)
            .collect();
        self.cached_layers = cached_layers;

        for cached_layer in &mut self.cached_layers.values_mut() {
            cached_layer.was_used = false;
        }

        self.composition.remove_disabled();
        self.composition.layers_mut().for_each(|layer| {
            layer.set_is_enabled(false);
        });

        self.index = 0;
    }
}

fn to_mold_point(p: math::Vec) -> Point<f32> {
    Point::new(p.x, p.y)
}

fn to_mold_path(commands: &[Command]) -> Path {
    let mut path = Path::new();
    let mut end_point = None;

    for command in commands {
        match *command {
            Command::MoveTo(p) => end_point = Some(p),
            Command::LineTo(p) => {
                path.line(to_mold_point(end_point.unwrap()), to_mold_point(p));
                end_point = Some(p);
            }
            Command::CubicTo(c0, c1, p) => {
                path.cubic(
                    to_mold_point(end_point.unwrap()),
                    to_mold_point(c0),
                    to_mold_point(c1),
                    to_mold_point(p),
                );
                end_point = Some(p);
            }
            Command::Close => path.close(),
        }
    }

    path
}

impl Renderer for MoldRenderer {
    fn draw(&mut self, path: &CommandPath, transform: Mat, paint: &RenderPaint) {
        fn transform_scales_up(transform: &Mat) -> bool {
            transform.scale_x * transform.scale_x + transform.shear_y * transform.shear_y > 1.001
                || transform.shear_x * transform.shear_x + transform.scale_y * transform.scale_y
                    > 1.001
        }

        let layer = match path
            .user_tag
            .get()
            .and_then(|tag| self.cached_layers.get_mut(&tag))
            .map(|cached_layer| {
                cached_layer.was_used = true;
                (
                    cached_layer.layer_id,
                    transform * cached_layer.inverted_transform,
                )
            }) {
            Some((layer_id, transform)) if !transform_scales_up(&transform) => self
                .composition
                .get_mut(layer_id)
                .unwrap()
                .set_is_enabled(true)
                .set_transform(&[
                    transform.scale_x,
                    transform.shear_x,
                    transform.shear_y,
                    transform.scale_y,
                    transform.translate_x,
                    transform.translate_y,
                ]),
            _ => {
                let mold_path = to_mold_path(&path.commands);
                let layer_id = self.composition.create_layer().unwrap();

                if let Some(inverted_transform) = transform.invert() {
                    let tag = self.tag();

                    path.user_tag.set(Some(tag));
                    self.cached_layers.insert(
                        tag,
                        CachedLayer {
                            layer_id,
                            inverted_transform,
                            was_used: true,
                        },
                    );
                }

                self.composition.insert_in_layer_transformed(
                    layer_id,
                    &mold_path,
                    &[
                        transform.scale_x,
                        transform.shear_x,
                        transform.translate_x,
                        transform.shear_y,
                        transform.scale_y,
                        transform.translate_y,
                        0.0,
                        0.0,
                        1.0,
                    ],
                )
            }
        };

        let blend_mode = match paint.blend_mode {
            rive::shapes::paint::BlendMode::SrcOver => BlendMode::Over,
            rive::shapes::paint::BlendMode::Screen => BlendMode::Screen,
            rive::shapes::paint::BlendMode::Overlay => BlendMode::Overlay,
            rive::shapes::paint::BlendMode::Darken => BlendMode::Darken,
            rive::shapes::paint::BlendMode::Lighten => BlendMode::Lighten,
            rive::shapes::paint::BlendMode::ColorDodge => BlendMode::ColorDodge,
            rive::shapes::paint::BlendMode::ColorBurn => BlendMode::ColorBurn,
            rive::shapes::paint::BlendMode::HardLight => BlendMode::HardLight,
            rive::shapes::paint::BlendMode::SoftLight => BlendMode::SoftLight,
            rive::shapes::paint::BlendMode::Difference => BlendMode::Difference,
            rive::shapes::paint::BlendMode::Exclusion => BlendMode::Exclusion,
            rive::shapes::paint::BlendMode::Multiply => BlendMode::Multiply,
            _ => BlendMode::Over,
        };

        layer
            .set_order(self.index as u16)
            .set_props(match &paint.color {
                PaintColor::Solid(color) => mold::Props {
                    fill_rule: mold::FillRule::NonZero,
                    func: mold::Func::Draw(mold::Style {
                        is_clipped: false,
                        fill_rule: mold::FillRule::NonZero,
                        fill: Fill::Solid(to_linear(*color)),
                        blend_mode,
                    }),
                },
                PaintColor::Gradient(gradient) => {
                    let start = transform * gradient.start;
                    let end = transform * gradient.end;

                    let mut builder = GradientBuilder::new([start.x, start.y], [end.x, end.y]);
                    builder.r#type(match gradient.r#type {
                        rive::GradientType::Linear => GradientType::Linear,
                        rive::GradientType::Radial => GradientType::Radial,
                    });

                    for &(color, stop) in gradient.stops.iter() {
                        builder.color_with_stop(to_linear(color), stop);
                    }

                    mold::Props {
                        fill_rule: mold::FillRule::NonZero,
                        func: mold::Func::Draw(mold::Style {
                            is_clipped: false,
                            fill_rule: mold::FillRule::NonZero,
                            fill: Fill::Gradient(builder.build().unwrap()),
                            blend_mode,
                        }),
                    }
                }
            });

        self.index += 1;
    }
}

pub fn rive_main<P: AsRef<path::Path>>(path: P) {
    let buffer = fs::read(path).unwrap();
    let mut reader = BinaryReader::new(&buffer);
    let file = File::import(&mut reader).unwrap();

    let artboard = file.artboard().unwrap();
    let artboard_ref = artboard.as_ref();
    artboard_ref.advance(0.0);

    let mut animation_instance = artboard_ref
        .first_animation::<LinearAnimation>()
        .map(|linear_animation| LinearAnimationInstance::new(linear_animation));

    let width = 1440;
    let height = 1440;

    let mut mold_renderer = MoldRenderer::new(width, height);
    let mut stylings = Vec::new();
    let mut last_instant = Instant::now();

    loop {
        let now = Instant::now();
        let elapsed = (now - last_instant).as_secs_f32();
        last_instant = now;

        if let Some(ref mut animation_instance) = animation_instance {
            // animation_instance.advance(0.0);
            animation_instance.advance(elapsed);
            animation_instance.apply(artboard.clone(), 1.0);
        }

        artboard_ref.advance(elapsed);
        artboard_ref.draw(
            &mut mold_renderer,
            layout::align(
                Fit::Contain,
                Alignment::center(),
                Aabb::new(0.0, 0.0, width as f32, height as f32),
                artboard.as_ref().bounds(),
            ),
        );

        mold_renderer.composition.compute_orders();
        stylings.clear();
        for (order, props) in mold_renderer.composition.props() {
            stylings.resize(order as usize + 1, Styling::default());

            let style = match &props.func {
                mold::Func::Draw(style) => style,
                mold::Func::Clip(_) => unimplemented!(),
            };

            let fill = match &style.fill {
                Fill::Solid(color) => *color,
                Fill::Gradient(gradient) => gradient.stops[0].0,
            };

            stylings[order as usize] = Styling {
                fill: [fill[2], fill[1], fill[0], fill[3]],
                fill_rule: props.fill_rule as u32,
                blend_mode: style.blend_mode as u32,
                ..Default::default()
            };
        }

        mold_renderer
            .cassia
            .render(mold_renderer.composition.pixel_segments(), &stylings);

        mold_renderer.reset();

        // break;
    }
}
