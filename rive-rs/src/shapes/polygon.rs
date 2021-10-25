use crate::{
    core::{Core, ObjectRef, OnAdded, Property},
    shapes::ParametricPath,
};

#[derive(Debug, Default)]
pub struct Polygon {
    parametric_path: ParametricPath,
    points: Property<u64>,
    corner_radius: Property<f32>,
}

impl ObjectRef<'_, Polygon> {
    pub fn points(&self) -> u64 {
        self.points.get()
    }

    pub fn set_points(&self, points: u64) {
        self.points.set(points);
        // Core::cast::<Path>(self).mark_path_dirty();
    }

    pub fn corner_radius(&self) -> f32 {
        self.corner_radius.get()
    }

    pub fn set_corner_radius(&self, corner_radius: f32) {
        self.corner_radius.set(corner_radius);
    }
}

impl Core for Polygon {
    parent_types![(parametric_path, ParametricPath)];

    properties!(
        (125, points, set_points),
        (126, corner_radius, set_corner_radius),
        parametric_path
    );
}

impl OnAdded for ObjectRef<'_, Polygon> {
    on_added!(ParametricPath);
}
