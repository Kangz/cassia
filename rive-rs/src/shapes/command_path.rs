use std::{cell::Cell, iter, num::NonZeroU64};

use smallvec::SmallVec;

use crate::{
    math::{self, Bezier, Mat},
    renderer::StrokeStyle,
    shapes::paint::{StrokeCap, StrokeJoin},
};

#[derive(Clone, Debug)]
pub struct CommandPathBuilder {
    commands: Vec<Command>,
}

impl CommandPathBuilder {
    pub fn new() -> Self {
        Self {
            commands: Vec::new(),
        }
    }

    pub fn move_to(&mut self, p: math::Vec) -> &mut Self {
        self.commands.push(Command::MoveTo(p));
        self
    }

    pub fn line_to(&mut self, p: math::Vec) -> &mut Self {
        self.commands.push(Command::LineTo(p));
        self
    }

    pub fn cubic_to(&mut self, c0: math::Vec, c1: math::Vec, p: math::Vec) -> &mut Self {
        self.commands.push(Command::CubicTo(c0, c1, p));
        self
    }

    pub fn rect(&mut self, p: math::Vec, size: math::Vec) -> &mut Self {
        self.move_to(p)
            .line_to(p + math::Vec::new(size.x, 0.0))
            .line_to(p + math::Vec::new(size.x, size.y))
            .line_to(p + math::Vec::new(0.0, size.y))
            .close()
    }

    pub fn path(&mut self, path: &CommandPath, t: Mat) -> &mut Self {
        self.commands
            .extend(path.commands.iter().map(|&command| match command {
                Command::MoveTo(p) => Command::MoveTo(t * p),
                Command::LineTo(p) => Command::LineTo(t * p),
                Command::CubicTo(c0, c1, p) => Command::CubicTo(t * c0, t * c1, t * p),
                Command::Close => Command::Close,
            }));
        self
    }

    pub fn close(&mut self) -> &mut Self {
        self.commands.push(Command::Close);
        self
    }

    pub fn build(self) -> CommandPath {
        CommandPath {
            commands: self.commands,
            user_tag: Cell::new(None),
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub enum Command {
    MoveTo(math::Vec),
    LineTo(math::Vec),
    CubicTo(math::Vec, math::Vec, math::Vec),
    Close,
}

#[derive(Clone, Debug)]
pub struct CommandPath {
    pub commands: Vec<Command>,
    pub user_tag: Cell<Option<NonZeroU64>>,
}

impl CommandPath {
    pub fn outline_strokes(&self, style: &StrokeStyle) -> Self {
        let mut commands = Vec::new();

        let mut curves = Vec::new();
        let mut end_point = None;

        for command in &self.commands {
            match *command {
                Command::MoveTo(p) => {
                    if !curves.is_empty() {
                        if let Some(outline) = Outline::new(&curves, false, style) {
                            commands.extend(outline.as_commands());
                        }

                        curves.clear();
                    }

                    end_point = Some(p);
                }
                Command::LineTo(p) => {
                    curves.push(Bezier::Line([end_point.unwrap(), p]));
                    end_point = Some(p);
                }
                Command::CubicTo(c0, c1, p) => {
                    curves.push(Bezier::Cubic([end_point.unwrap(), c0, c1, p]));
                    end_point = Some(p);
                }
                Command::Close => {
                    if let (Some(first), Some(last)) = (
                        curves.first().and_then(|c| c.points().first().copied()),
                        curves.last().and_then(|c| c.points().last().copied()),
                    ) {
                        if first.distance(last) > 0.01 {
                            curves.push(Bezier::Line([last, first]));
                        }
                    }

                    if let Some(outline) = Outline::new(&curves, true, &style) {
                        commands.extend(outline.as_commands());
                    }

                    curves.clear();
                    end_point = None;
                }
            }
        }

        if !curves.is_empty() {
            if let Some(outline) = Outline::new(&curves, false, &style) {
                commands.extend(outline.as_commands());
            }
        }

        Self {
            commands,
            user_tag: Cell::new(None),
        }
    }
}

#[derive(Debug)]
struct Line {
    point: math::Vec,
    angle: f32,
}

impl Line {
    pub fn new(p0: math::Vec, p1: math::Vec) -> Self {
        let diff = p1 - p0;
        Self {
            point: p1,
            angle: diff.y.atan2(diff.x),
        }
    }

    fn angle_vec(&self) -> math::Vec {
        let (sin, cos) = self.angle.sin_cos();
        math::Vec::new(cos, sin)
    }

    pub fn intersect(&self, other: &Self) -> math::Vec {
        let d0 = self.angle_vec();
        let d1 = other.angle_vec();
        let d = d0 - d1;

        let t = if d.x != 0.0 {
            (other.point.x - self.point.x) / d.x
        } else {
            (other.point.y - self.point.y) / d.y
        };

        self.point + math::Vec::new(d0.x, d0.y) * t
    }

    pub fn angle_diff(&self, other: &Self) -> f32 {
        let diff = self.angle - other.angle;
        let (sin, cos) = diff.sin_cos();

        sin.atan2(cos).abs()
    }

    pub fn project(&self, dist: f32) -> math::Vec {
        self.point + self.angle_vec() * dist
    }

    pub fn mid(&self, other: &Self) -> Self {
        let mid_angle = (self.angle_vec() + other.angle_vec()) * 0.5;
        Self {
            point: (self.point + other.point) * 0.5,
            angle: mid_angle.y.atan2(mid_angle.x),
        }
    }
}

const MITER_LIMIT: f32 = 10.0;

#[derive(Debug)]
struct Outline {
    curves: Vec<Bezier>,
    second_outline_index: Option<usize>,
}

impl Outline {
    fn last_first_line(&self, next_curves: &[Bezier]) -> (Line, Line) {
        let last = self.curves.last().unwrap();
        let first = next_curves.first().unwrap();

        let [p0, p1] = last.right_different();
        let last_line = Line::new(p0, p1);
        let [p0, p1] = first.left_different();
        let first_line = Line::new(p1, p0);

        (last_line, first_line)
    }

    fn join(&mut self, next_curves: &mut [Bezier], dist: f32, join: StrokeJoin) {
        let last = self.curves.last_mut().unwrap();
        let first = next_curves.first_mut().unwrap();

        if !last.intersect(first) {
            let (last_line, first_line) = self.last_first_line(&next_curves);
            let mid_line = last_line.mid(&first_line);

            match join {
                StrokeJoin::Bevel => {
                    self.curves
                        .push(Bezier::Line([last_line.point, first_line.point]));
                }
                StrokeJoin::Miter => {
                    let intersection = last_line.intersect(&first_line);

                    if last_line.angle_diff(&first_line) >= MITER_LIMIT.recip().asin() * 2.0 {
                        self.curves
                            .push(Bezier::Line([last_line.point, intersection]));
                        self.curves
                            .push(Bezier::Line([intersection, first_line.point]));
                    } else {
                        self.curves
                            .push(Bezier::Line([last_line.point, first_line.point]));
                    }
                }
                StrokeJoin::Round => {
                    let angle = std::f32::consts::PI - last_line.angle_diff(&first_line);
                    if angle < std::f32::consts::FRAC_PI_2 {
                        self.curves.push(Bezier::Cubic([
                            last_line.point,
                            last_line.project(math::arc_constant(angle) * dist),
                            first_line.project(math::arc_constant(angle) * dist),
                            first_line.point,
                        ]));
                    } else {
                        let angle = angle / 2.0;
                        let mid_dist = (1.0 - angle.cos()) * dist;
                        let mid = mid_line.project(mid_dist);

                        let mid_left = Line {
                            point: mid,
                            angle: mid_line.angle + std::f32::consts::FRAC_PI_2,
                        };
                        let mid_right = Line {
                            point: mid,
                            angle: mid_line.angle - std::f32::consts::FRAC_PI_2,
                        };

                        self.curves.push(Bezier::Cubic([
                            last_line.point,
                            last_line.project(math::arc_constant(angle) * dist),
                            mid_left.project(math::arc_constant(angle) * dist),
                            mid,
                        ]));
                        self.curves.push(Bezier::Cubic([
                            mid,
                            mid_right.project(math::arc_constant(angle) * dist),
                            first_line.project(math::arc_constant(angle) * dist),
                            first_line.point,
                        ]));
                    }
                }
            }
        }
    }

    fn join_curves(
        &mut self,
        mut offset_curves: impl Iterator<Item = SmallVec<[Bezier; 16]>>,
        dist: f32,
        join: StrokeJoin,
        is_closed: bool,
    ) {
        let start_index = self.curves.len();
        self.curves.extend(offset_curves.next().unwrap());

        for mut next_curves in offset_curves {
            self.join(&mut next_curves, dist, join);
            self.curves.extend(next_curves);
        }

        if is_closed {
            let mut next_curves = [self.curves[start_index].clone()];
            self.join(&mut next_curves, dist, join);
            self.curves[start_index] = next_curves[0].clone();
        }
    }

    fn cap_end(&mut self, last_line: &Line, first_line: &Line, dist: f32, cap: StrokeCap) {
        match cap {
            StrokeCap::Butt => {
                self.curves
                    .push(Bezier::Line([last_line.point, first_line.point]));
            }
            StrokeCap::Square => {
                let projected_last = last_line.project(dist);
                let projected_first = first_line.project(dist);

                self.curves
                    .push(Bezier::Line([last_line.point, projected_last]));
                self.curves
                    .push(Bezier::Line([projected_last, projected_first]));
                self.curves
                    .push(Bezier::Line([projected_first, first_line.point]));
            }
            StrokeCap::Round => {
                let mid_line = last_line.mid(&first_line);
                let mid = mid_line.project(dist);

                let mid_left = Line {
                    point: mid,
                    angle: mid_line.angle + std::f32::consts::FRAC_PI_2,
                };
                let mid_right = Line {
                    point: mid,
                    angle: mid_line.angle - std::f32::consts::FRAC_PI_2,
                };

                self.curves.push(Bezier::Cubic([
                    last_line.point,
                    last_line.project(math::arc_constant(std::f32::consts::FRAC_PI_2) * dist),
                    mid_left.project(math::arc_constant(std::f32::consts::FRAC_PI_2) * dist),
                    mid,
                ]));
                self.curves.push(Bezier::Cubic([
                    mid,
                    mid_right.project(math::arc_constant(std::f32::consts::FRAC_PI_2) * dist),
                    first_line.project(math::arc_constant(std::f32::consts::FRAC_PI_2) * dist),
                    first_line.point,
                ]));
            }
        }
    }

    pub fn new(curves: &[Bezier], is_closed: bool, style: &StrokeStyle) -> Option<Self> {
        if curves.is_empty() {
            return None;
        }

        let mut outline = Self {
            curves: Vec::new(),
            second_outline_index: None,
        };

        let dist = style.thickness / 2.0;

        if !is_closed {
            outline.join_curves(
                curves
                    .iter()
                    .filter_map(Bezier::normalize)
                    .map(|curve| curve.offset(dist)),
                dist,
                style.join,
                is_closed,
            );

            let mut flipside_curves = curves
                .iter()
                .rev()
                .filter_map(Bezier::normalize)
                .map(|curve| curve.offset(-dist))
                .peekable();

            let (last_line, first_line) = outline.last_first_line(&flipside_curves.peek().unwrap());
            outline.cap_end(&last_line, &first_line, dist, style.cap);

            outline.join_curves(flipside_curves, dist, style.join, is_closed);

            let (last_line, first_line) = outline.last_first_line(&outline.curves);
            outline.cap_end(&last_line, &first_line, dist, style.cap);
        } else {
            outline.join_curves(
                curves
                    .iter()
                    .filter_map(Bezier::normalize)
                    .map(|curve| curve.offset(dist)),
                dist,
                style.join,
                is_closed,
            );
            outline.second_outline_index = Some(outline.curves.len());
            outline.join_curves(
                curves
                    .iter()
                    .rev()
                    .filter_map(Bezier::normalize)
                    .map(|curve| curve.offset(-dist)),
                dist,
                style.join,
                is_closed,
            );
        }

        Some(outline)
    }

    pub fn as_commands(&self) -> impl Iterator<Item = Command> + '_ {
        let mid_move = self
            .second_outline_index
            .map(|i| Command::MoveTo(self.curves[i].points()[0]));
        let as_commands = |curve: &Bezier| match *curve {
            Bezier::Line([_, p1]) => Command::LineTo(p1),
            Bezier::Cubic([_, p1, p2, p3]) => Command::CubicTo(p1, p2, p3),
        };

        iter::once(Command::MoveTo(self.curves[0].points()[0]))
            .chain(
                self.curves[..self.second_outline_index.unwrap_or_default()]
                    .iter()
                    .map(as_commands),
            )
            .chain(self.second_outline_index.map(|_| Command::Close))
            .chain(mid_move)
            .chain(
                self.curves[self.second_outline_index.unwrap_or_default()..]
                    .iter()
                    .map(as_commands),
            )
            .chain(iter::once(Command::Close))
    }
}
