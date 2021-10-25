use crate::{
    core::{Core, ObjectRef, OnAdded},
    shapes::ParametricPath,
};

#[derive(Debug, Default)]
pub struct Rectangle {
    parametric_path: ParametricPath,
}

impl Core for Rectangle {
    parent_types![(parametric_path, ParametricPath)];

    properties!(parametric_path);
}

impl OnAdded for ObjectRef<'_, Rectangle> {
    on_added!(ParametricPath);
}
