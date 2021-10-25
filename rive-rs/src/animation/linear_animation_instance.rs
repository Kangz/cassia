use crate::{
    animation::{LinearAnimation, Loop},
    artboard::Artboard,
    core::Object,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Direction {
    Forwards,
    Backwards,
}

impl Into<f32> for Direction {
    fn into(self) -> f32 {
        match self {
            Self::Forwards => 1.0,
            Self::Backwards => -1.0,
        }
    }
}

#[derive(Debug)]
pub struct LinearAnimationInstance {
    linear_animation: Object<LinearAnimation>,
    time: f32,
    direction: Direction,
    did_loop: bool,
}

impl LinearAnimationInstance {
    pub fn new(linear_animation: Object<LinearAnimation>) -> Self {
        let time = linear_animation
            .as_ref()
            .enable_work_area()
            .then(|| {
                linear_animation.as_ref().work_start() as f32
                    / linear_animation.as_ref().fps() as f32
            })
            .unwrap_or_default();

        Self {
            linear_animation,
            time,
            direction: Direction::Forwards,
            did_loop: false,
        }
    }

    pub fn set_time(&mut self, time: f32) {
        if time == self.time {
            return;
        }

        self.time = time;
        self.direction = Direction::Forwards;
    }

    pub fn advance(&mut self, elapsed_seconds: f32) -> bool {
        let linear_animation = self.linear_animation.as_ref();
        let direction: f32 = self.direction.into();
        self.time += elapsed_seconds * linear_animation.speed() * direction;

        let fps = linear_animation.fps() as f32;
        let mut frames = self.time * fps;

        let start = linear_animation
            .enable_work_area()
            .then(|| linear_animation.work_start() as f32)
            .unwrap_or_default();
        let end = linear_animation
            .enable_work_area()
            .then(|| linear_animation.work_end() as f32)
            .unwrap_or_else(|| linear_animation.duration() as f32);
        let range = end - start;

        self.did_loop = false;
        let mut keep_going = true;

        match linear_animation.r#loop() {
            Loop::OneShot => {
                if frames > end {
                    keep_going = false;
                    frames = end;
                    self.time = frames / fps;
                    self.did_loop = true;
                }
            }
            Loop::Loop => {
                if frames >= end {
                    frames = start + (self.time * fps - start) % range;
                    self.time = frames / fps;
                    self.did_loop = true;
                }
            }
            Loop::PingPong => loop {
                if self.direction == Direction::Forwards && frames >= end {
                    self.direction = Direction::Backwards;
                    frames = end + (end - frames);
                    self.time = frames / fps;
                    self.did_loop = true;
                } else if self.direction == Direction::Backwards && frames < start {
                    self.direction = Direction::Forwards;
                    frames = start + (start - frames);
                    self.time = frames / fps;
                    self.did_loop = true;
                } else {
                    break;
                }
            },
        }

        keep_going
    }

    pub fn apply(&self, artboard: Object<Artboard>, mix: f32) {
        self.linear_animation
            .as_ref()
            .apply(artboard, self.time, mix);
    }
}
