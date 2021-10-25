use crate::{
    component::Component,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct ContainerComponent {
    component: Component,
}

impl Core for ContainerComponent {
    parent_types![(component, Component)];

    properties!(component);
}

impl OnAdded for ObjectRef<'_, ContainerComponent> {
    on_added!(Component);
}
