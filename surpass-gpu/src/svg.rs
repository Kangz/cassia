use std::{mem, path::Path};

use mold::{Composition, FillRule, Point};
use painter::{CompactPixelSegment, Style};
use svg::{node::element::tag, parser::Event};
use svgtypes::{Color, PathParser, PathSegment, Transform, ViewBox};

fn reflect(point: Point<f32>, against: Point<f32>) -> Point<f32> {
    Point::new(against.x * 2.0 - point.x, against.y * 2.0 - point.y)
}

struct EllipticalArc {
    cx: f64,
    cy: f64,
    rx: f64,
    ry: f64,
    x_axis_rotation: f64,
    angle: f64,
    angle_delta: f64,
}

fn convert_to_center(
    rx: f64,
    ry: f64,
    x_axis_rotation: f64,
    large_arc: bool,
    sweep: bool,
    x0: f64,
    y0: f64,
    x1: f64,
    y1: f64,
) -> Option<EllipticalArc> {
    if x0 == x1 && y0 == y1 {
        return None;
    }

    let rx = rx.abs();
    let ry = ry.abs();

    if rx == 0.0 || ry == 0.0 {
        return None;
    }

    let cos_phi = x_axis_rotation.cos();
    let sin_phi = x_axis_rotation.sin();

    let x0 = (x0 * cos_phi + y0 * sin_phi) / rx;
    let y0 = (-x0 * sin_phi + y0 * cos_phi) / ry;

    let x1 = (x1 * cos_phi + y1 * sin_phi) / rx;
    let y1 = (-x1 * sin_phi + y1 * cos_phi) / ry;

    let lx = (x0 - x1) * 0.5;
    let ly = (y0 - y1) * 0.5;

    let mut cx = (x0 + x1) * 0.5;
    let mut cy = (y0 + y1) * 0.5;

    let len_squared = lx * lx + ly * ly;
    if len_squared < 1.0 {
        let mut radicand = ((1.0 - len_squared) / len_squared).sqrt();
        if large_arc != sweep {
            radicand = -radicand;
        }

        cx += -ly * radicand;
        cy += lx * radicand;
    }

    let theta = (y0 - cy).atan2(x0 - cx);
    let mut delta_theta = (y1 - cy).atan2(x1 - cx) - theta;

    let cxs = cx * rx;
    let cys = cy * ry;

    cx = cxs * cos_phi - cys * sin_phi;
    cy = cxs * sin_phi + cys * cos_phi;

    if sweep {
        if delta_theta < 0.0 {
            delta_theta += std::f64::consts::PI * 2.0;
        }
    } else {
        if delta_theta > 0.0 {
            delta_theta -= std::f64::consts::PI * 2.0;
        }
    }

    Some(EllipticalArc {
        cx,
        cy,
        rx,
        ry,
        x_axis_rotation,
        angle: theta,
        angle_delta: delta_theta,
    })
}

fn to_linear(color: Color, alpha: f32) -> [f32; 4] {
    fn conv(l: u8) -> f32 {
        let l = l as f32 * 255.0f32.recip();

        if l <= 0.04045 {
            l * 12.92f32.recip()
        } else {
            ((l + 0.055) * 1.055f32.recip()).powf(2.4)
        }
    }

    [conv(color.blue), conv(color.green), conv(color.red), alpha]
}

#[derive(Debug, Default)]
struct Group {
    transform: Option<Transform>,
    fill: Option<Color>,
    opacity: Option<f32>,
}

#[derive(Debug)]
struct Svg {
    width: Option<usize>,
    height: Option<usize>,
    groups: Vec<Group>,
    paths: Vec<(mold::Path, FillRule, Option<[f32; 4]>, Point<f32>)>,
}

impl Svg {
    pub fn new() -> Self {
        Self {
            width: None,
            height: None,
            groups: vec![],
            paths: vec![],
        }
    }

    fn group_transform(&self) -> Option<&Transform> {
        self.groups
            .iter()
            .rev()
            .find_map(|group| group.transform.as_ref())
    }

    fn group_fill(&self) -> Option<Color> {
        self.groups.iter().rev().find_map(|group| group.fill)
    }

    fn groups_opacity(&self) -> f32 {
        self.groups
            .iter()
            .map(|group| group.opacity)
            .flatten()
            .product()
    }

    fn t(&self, point: Point<f32>) -> Point<f32> {
        let mut x = point.x as f64;
        let mut y = point.y as f64;

        if let Some(t) = self.group_transform() {
            t.apply_to(&mut x, &mut y);
        }

        Point::new(x as f32, y as f32)
    }

    fn push_rationals_from_arc(
        &self,
        path: &mut mold::Path,
        arc: &EllipticalArc,
        mut end_point: Point<f32>,
    ) -> Point<f32> {
        let mut angle = arc.angle;
        let mut angle_delta = arc.angle_delta;

        let cos_phi = arc.x_axis_rotation.cos();
        let sin_phi = arc.x_axis_rotation.sin();

        let angle_sweep = std::f64::consts::PI / 2.0;
        let angle_incr = if angle_delta > 0.0 {
            angle_sweep
        } else {
            -angle_sweep
        };

        while angle_delta != 0.0 {
            let theta = angle;
            let sweep = if angle_delta.abs() <= angle_sweep {
                angle_delta
            } else {
                angle_incr
            };

            angle += sweep;
            angle_delta -= sweep;

            let half_sweep = sweep * 0.5;
            let w = half_sweep.cos();

            let mut p1 = Point::new(
                (theta + half_sweep).cos() / w,
                (theta + half_sweep).sin() / w,
            );
            let mut p2 = Point::new((theta + sweep).cos(), (theta + sweep).sin());

            p1.x *= arc.rx;
            p1.y *= arc.ry;
            p2.x *= arc.rx;
            p2.y *= arc.ry;

            let p1 = Point::new(
                (arc.cx + p1.x * cos_phi - p1.y * sin_phi) as f32,
                (arc.cy + p1.x * sin_phi + p1.y * cos_phi) as f32,
            );
            let p2 = Point::new(
                (arc.cx + p2.x * cos_phi - p2.y * sin_phi) as f32,
                (arc.cy + p2.x * sin_phi + p2.y * cos_phi) as f32,
            );

            path.rat_quad(
                (self.t(end_point), 1.0),
                (self.t(p1), w as f32),
                (self.t(p2), 1.0),
            );

            end_point = p2;
        }

        end_point
    }

    pub fn open(&mut self, path: impl AsRef<Path>) {
        for event in svg::open(path).unwrap() {
            match event {
                Event::Tag(tag::SVG, tag::Type::Start, attrs) => {
                    if let Some(view_box) = attrs.get("viewBox") {
                        let view_box: ViewBox = view_box.parse().unwrap();
                        self.width = Some(view_box.w.ceil() as usize);
                        self.height = Some(view_box.h.ceil() as usize);
                    }
                    if let Some(width) = attrs.get("width") {
                        self.width = width.parse().map(|v: f64| v.ceil() as usize).ok();
                    }
                    if let Some(height) = attrs.get("height") {
                        self.height = height.parse().map(|v: f64| v.ceil() as usize).ok();
                    }
                }
                Event::Tag(tag::Group, tag::Type::Start, attrs) => {
                    let mut group = Group::default();

                    group.transform = attrs
                        .get("transform")
                        .and_then(|transform| transform.parse().ok());
                    group.fill = attrs.get("fill").and_then(|fill| fill.parse().ok());
                    group.opacity = attrs
                        .get("opacity")
                        .and_then(|opacity| opacity.parse().ok());

                    self.groups.push(group);
                }
                Event::Tag(tag::Group, tag::Type::End, _) => {
                    self.groups.pop();
                }
                Event::Tag(tag::Path, _, attrs) => {
                    if attrs
                        .get("stroke")
                        .filter(|val| val.to_string() != "none")
                        .is_some()
                    {
                        continue;
                    }

                    let mut path = mold::Path::new();

                    let color: Option<Color> = attrs
                        .get("fill")
                        .and_then(|fill| fill.parse().ok())
                        .or(self.group_fill())
                        .or(Some(Color::black()));

                    let data = match attrs.get("d") {
                        Some(data) => data,
                        None => continue,
                    };

                    let mut start_point = None;
                    let mut end_point = Point::new(0.0, 0.0);
                    let mut mid_point = Point::new(0.0, 0.0);
                    let mut num_points = 0usize;
                    let mut quad_control_point = None;
                    let mut cubic_control_point = None;

                    let point = |x, y| Point::new(x as f32, y as f32);
                    let add_diff = |end_point: Point<f32>, x, y| {
                        Point::new(end_point.x + x as f32, end_point.y + y as f32)
                    };

                    for segment in PathParser::from(data.to_string().as_str()) {
                        match segment.unwrap() {
                            PathSegment::MoveTo { abs: true, x, y } => {
                                if start_point.take().is_some() {
                                    path.close();
                                }

                                end_point = point(x, y);
                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::MoveTo { abs: false, x, y } => {
                                if start_point.take().is_some() {
                                    path.close();
                                }

                                end_point = add_diff(end_point, x, y);
                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::LineTo { abs: true, x, y } => {
                                let p0 = point(x, y);

                                path.line(self.t(end_point), self.t(p0));

                                start_point.get_or_insert(end_point);
                                end_point = p0;
                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::LineTo { abs: false, x, y } => {
                                let p0 = add_diff(end_point, x, y);

                                path.line(self.t(end_point), self.t(p0));

                                start_point.get_or_insert(end_point);
                                end_point = p0;
                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::HorizontalLineTo { abs: true, x } => {
                                let p0 = point(x, end_point.y as f64);

                                path.line(self.t(end_point), self.t(p0));

                                start_point.get_or_insert(end_point);
                                end_point = p0;
                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::HorizontalLineTo { abs: false, x } => {
                                let p0 = add_diff(end_point, x, 0.0);

                                path.line(self.t(end_point), self.t(p0));

                                start_point.get_or_insert(end_point);
                                end_point = p0;
                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::VerticalLineTo { abs: true, y } => {
                                let p0 = point(end_point.x as f64, y);

                                path.line(self.t(end_point), self.t(p0));

                                start_point.get_or_insert(end_point);
                                end_point = p0;
                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::VerticalLineTo { abs: false, y } => {
                                let p0 = add_diff(end_point, 0.0, y);

                                path.line(self.t(end_point), self.t(p0));

                                start_point.get_or_insert(end_point);
                                end_point = p0;
                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::Quadratic {
                                abs: true,
                                x1,
                                y1,
                                x,
                                y,
                            } => {
                                let p0 = point(x1, y1);
                                let p1 = point(x, y);
                                let control_point = p0;

                                path.quad(self.t(end_point), self.t(control_point), self.t(p1));

                                start_point.get_or_insert(end_point);
                                end_point = p1;
                                quad_control_point = Some(control_point);
                                cubic_control_point = None;
                            }
                            PathSegment::Quadratic {
                                abs: false,
                                x1,
                                y1,
                                x,
                                y,
                            } => {
                                let p0 = add_diff(end_point, x1, y1);
                                let p1 = add_diff(end_point, x, y);
                                let control_point = p0;

                                path.quad(self.t(end_point), self.t(control_point), self.t(p1));

                                start_point.get_or_insert(end_point);
                                end_point = p1;
                                quad_control_point = Some(control_point);
                                cubic_control_point = None;
                            }
                            PathSegment::CurveTo {
                                abs: true,
                                x1,
                                y1,
                                x2,
                                y2,
                                x,
                                y,
                            } => {
                                let p0 = point(x1, y1);
                                let p1 = point(x2, y2);
                                let p2 = point(x, y);
                                let control_point = p1;

                                path.cubic(
                                    self.t(end_point),
                                    self.t(p0),
                                    self.t(control_point),
                                    self.t(p2),
                                );

                                start_point.get_or_insert(end_point);
                                end_point = p2;
                                quad_control_point = None;
                                cubic_control_point = Some(control_point);
                            }
                            PathSegment::CurveTo {
                                abs: false,
                                x1,
                                y1,
                                x2,
                                y2,
                                x,
                                y,
                            } => {
                                let p0 = add_diff(end_point, x1, y1);
                                let p1 = add_diff(end_point, x2, y2);
                                let p2 = add_diff(end_point, x, y);
                                let control_point = p1;

                                path.cubic(
                                    self.t(end_point),
                                    self.t(p0),
                                    self.t(control_point),
                                    self.t(p2),
                                );

                                start_point.get_or_insert(end_point);
                                end_point = p2;
                                quad_control_point = None;
                                cubic_control_point = Some(control_point);
                            }
                            PathSegment::SmoothQuadratic { abs: true, x, y } => {
                                let p1 = point(x, y);
                                let control_point =
                                    reflect(quad_control_point.unwrap_or(end_point), end_point);

                                path.quad(self.t(end_point), self.t(control_point), self.t(p1));

                                start_point.get_or_insert(end_point);
                                end_point = p1;
                                quad_control_point = Some(control_point);
                                cubic_control_point = None;
                            }
                            PathSegment::SmoothQuadratic { abs: false, x, y } => {
                                let p1 = add_diff(end_point, x, y);
                                let control_point =
                                    reflect(quad_control_point.unwrap_or(end_point), end_point);

                                path.quad(self.t(end_point), self.t(control_point), self.t(p1));

                                start_point.get_or_insert(end_point);
                                end_point = p1;
                                quad_control_point = Some(control_point);
                                cubic_control_point = None;
                            }
                            PathSegment::SmoothCurveTo {
                                abs: true,
                                x2,
                                y2,
                                x,
                                y,
                            } => {
                                let p1 = point(x2, y2);
                                let p2 = point(x, y);
                                let control_point =
                                    reflect(cubic_control_point.unwrap_or(end_point), end_point);

                                path.cubic(
                                    self.t(end_point),
                                    self.t(control_point),
                                    self.t(p1),
                                    self.t(p2),
                                );

                                start_point.get_or_insert(end_point);
                                end_point = p2;
                                quad_control_point = None;
                                cubic_control_point = Some(control_point);
                            }
                            PathSegment::SmoothCurveTo {
                                abs: false,
                                x2,
                                y2,
                                x,
                                y,
                            } => {
                                let p1 = add_diff(end_point, x2, y2);
                                let p2 = add_diff(end_point, x, y);
                                let control_point =
                                    reflect(cubic_control_point.unwrap_or(end_point), end_point);

                                path.cubic(
                                    self.t(end_point),
                                    self.t(control_point),
                                    self.t(p1),
                                    self.t(p2),
                                );

                                start_point.get_or_insert(end_point);
                                end_point = p2;
                                quad_control_point = None;
                                cubic_control_point = Some(control_point);
                            }
                            PathSegment::EllipticalArc {
                                abs: true,
                                rx,
                                ry,
                                x_axis_rotation,
                                large_arc,
                                sweep,
                                x,
                                y,
                            } => {
                                let arc = convert_to_center(
                                    rx,
                                    ry,
                                    x_axis_rotation,
                                    large_arc,
                                    sweep,
                                    end_point.x as f64,
                                    end_point.y as f64,
                                    x,
                                    y,
                                );

                                if let Some(arc) = arc {
                                    let new_end_point =
                                        self.push_rationals_from_arc(&mut path, &arc, end_point);

                                    start_point.get_or_insert(end_point);
                                    end_point = new_end_point;
                                }

                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::EllipticalArc {
                                abs: false,
                                rx,
                                ry,
                                x_axis_rotation,
                                large_arc,
                                sweep,
                                x,
                                y,
                            } => {
                                let p0 = add_diff(end_point, x, y);
                                let arc = convert_to_center(
                                    rx,
                                    ry,
                                    x_axis_rotation,
                                    large_arc,
                                    sweep,
                                    end_point.x as f64,
                                    end_point.y as f64,
                                    p0.x as f64,
                                    p0.y as f64,
                                );

                                if let Some(arc) = arc {
                                    let new_end_point =
                                        self.push_rationals_from_arc(&mut path, &arc, end_point);

                                    start_point.get_or_insert(end_point);
                                    end_point = new_end_point;
                                }

                                quad_control_point = None;
                                cubic_control_point = None;
                            }
                            PathSegment::ClosePath { .. } => {
                                path.close();
                                if let Some(start_point) = start_point.take() {
                                    end_point = start_point;
                                    quad_control_point = None;
                                    cubic_control_point = None;
                                }
                            }
                        }

                        let diff = self.t(end_point);

                        mid_point = Point::new(mid_point.x + diff.x, mid_point.y + diff.y);
                        num_points += 1;
                    }

                    if start_point.take().is_some() {
                        path.close();
                    }

                    let fill_command = if let Some(fill_rule) = attrs.get("fill-rule") {
                        if &fill_rule.to_string() == "evenodd" {
                            FillRule::EvenOdd
                        } else {
                            FillRule::NonZero
                        }
                    } else {
                        FillRule::NonZero
                    };

                    let opacity: f32 = attrs
                        .get("opacity")
                        .and_then(|opacity| opacity.parse().ok())
                        .unwrap_or_else(|| self.groups_opacity());

                    let color = color.map(|color| to_linear(color, opacity));

                    mid_point = Point::new(
                        mid_point.x / num_points as f32,
                        mid_point.y / num_points as f32,
                    );

                    self.paths.push((path, fill_command, color, mid_point));
                }
                Event::Tag(tag::Rectangle, tag::Type::Start, attrs) => {
                    if attrs
                        .get("stroke")
                        .filter(|val| val.to_string() != "none")
                        .is_some()
                    {
                        continue;
                    }

                    let mut path = mold::Path::new();

                    let x: f32 = attrs.get("x").expect("rect missing x").parse().unwrap();
                    let y: f32 = attrs.get("y").expect("rect missing y").parse().unwrap();
                    let width: f32 = attrs
                        .get("width")
                        .expect("rect missing width")
                        .parse()
                        .unwrap();
                    let height: f32 = attrs
                        .get("height")
                        .expect("rect missing height")
                        .parse()
                        .unwrap();

                    path.line(Point::new(x, y), Point::new(x, y + height));
                    path.line(Point::new(x, y + height), Point::new(x + width, y + height));
                    path.line(Point::new(x + width, y + height), Point::new(x + width, y));
                    path.line(Point::new(x + width, y), Point::new(x, y));

                    let color: Option<Color> = attrs
                        .get("fill")
                        .and_then(|fill| fill.parse().ok())
                        .or(self.group_fill())
                        .or(Some(Color::black()));

                    let fill_command = if let Some(fill_rule) = attrs.get("fill-rule") {
                        if &fill_rule.to_string() == "evenodd" {
                            FillRule::EvenOdd
                        } else {
                            FillRule::NonZero
                        }
                    } else {
                        FillRule::NonZero
                    };

                    let opacity: f32 = attrs
                        .get("opacity")
                        .and_then(|opacity| opacity.parse().ok())
                        .unwrap_or_else(|| self.groups_opacity());

                    let color = color.map(|color| to_linear(color, opacity));
                    self.paths.push((
                        path,
                        fill_command,
                        color,
                        Point::new(x + width / 2.0, y + height / 2.0),
                    ));
                }
                _ => (),
            }
        }
    }

    pub fn width(&self) -> usize {
        self.width.expect("SVG requires explicit width/height")
    }

    pub fn height(&self) -> usize {
        self.height.expect("SVG requires explicit width/height")
    }
}

pub fn svg(input: &Path, scale: f32) -> (Vec<CompactPixelSegment>, Vec<Style>) {
    let mut svg = Svg::new();
    let mut composition = Composition::new();
    let mut styles = Vec::new();
    let transform = &[scale, 0.0, 0.0, 0.0, scale, 0.0, 0.0, 0.0, 1.0];

    svg.open(input);

    for (path, fill_rule, color, center) in svg.paths.iter().take(u16::max_value() as usize) {
        if let Some(color) = color {
            let layer_id = composition.create_layer().unwrap();
            composition.insert_in_layer_transformed(layer_id, path, transform);

            styles.push(Style {
                fill_rule: *fill_rule as u32,
                color: painter::Color {
                    r: color[2],
                    g: color[1],
                    b: color[0],
                    a: color[3],
                },
                blend_mode: 0,
            });
        }
    }

    (
        unsafe { mem::transmute(composition.pixel_segments().to_owned()) },
        styles,
    )
}
