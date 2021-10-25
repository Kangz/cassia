use std::fmt;

use crate::shapes::{CommandPath, MetricsPath};

pub trait StrokeEffect: fmt::Debug {
    fn effect_path(&self, source: &MetricsPath) -> CommandPath;
    fn invalidate_effect(&self);
}
