use crate::{
    core::{Core, ObjectRef, OnAdded},
    shapes::paint::LinearGradient,
};

#[derive(Debug, Default)]
pub struct RadialGradient {
    linear_gradient: LinearGradient,
}

impl Core for RadialGradient {
    parent_types![(linear_gradient, LinearGradient)];

    properties!(linear_gradient);
}

impl OnAdded for ObjectRef<'_, RadialGradient> {
    on_added!(LinearGradient);
}
