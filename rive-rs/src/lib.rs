#[macro_use]
mod core;

pub mod animation;
mod artboard;
mod backboard;
pub mod bones;
mod component;
mod component_dirt;
mod container_component;
mod dependency_sorter;
mod draw_rules;
mod draw_target;
mod drawable;
mod dyn_vec;
mod file;
pub mod layout;
pub mod math;
mod node;
mod option_cell;
mod renderer;
mod runtime_header;
pub mod shapes;
mod status_code;
mod transform_component;

pub use crate::core::BinaryReader;
pub use artboard::Artboard;
pub use backboard::Backboard;
pub use component::Component;
pub use container_component::ContainerComponent;
pub use draw_rules::DrawRules;
pub use draw_target::DrawTarget;
pub use drawable::Drawable;
pub use file::File;
pub use node::Node;
pub use renderer::{Gradient, GradientType, PaintColor, RenderPaint, Renderer, StrokeStyle, Style};
pub use status_code::StatusCode;
pub use transform_component::TransformComponent;
