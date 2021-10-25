use crate::{
    core::{Core, ObjectRef, OnAdded},
    shapes::ParametricPath,
};

#[derive(Debug, Default)]
pub struct Triangle {
    parametric_path: ParametricPath,
}

impl Core for Triangle {
    parent_types![(parametric_path, ParametricPath)];

    properties!(parametric_path);
}

impl OnAdded for ObjectRef<'_, Triangle> {
    on_added!(ParametricPath);
}
