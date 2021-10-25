use crate::{
    core::{Core, ObjectRef, OnAdded},
    transform_component::TransformComponent,
};

#[derive(Debug, Default)]
pub struct SkeletalComponent {
    transform_component: TransformComponent,
}

impl Core for SkeletalComponent {
    parent_types![(transform_component, TransformComponent)];

    properties!(transform_component);
}

impl OnAdded for ObjectRef<'_, SkeletalComponent> {
    on_added!(TransformComponent);
}
