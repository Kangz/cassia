use flagset::FlagSet;

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded},
    option_cell::OptionCell,
    shapes::{
        command_path::{CommandPath, CommandPathBuilder},
        PathSpace, Shape,
    },
    status_code::StatusCode,
    TransformComponent,
};

#[derive(Debug, Default)]
pub struct PathComposer {
    component: Component,
    shape: OptionCell<Object<Shape>>,
    local_path: OptionCell<CommandPath>,
    world_path: OptionCell<CommandPath>,
}

impl ObjectRef<'_, PathComposer> {
    pub(crate) fn with_local_path(&self, f: impl FnMut(Option<&CommandPath>)) {
        self.local_path.with(f);
    }

    pub(crate) fn with_world_path(&self, f: impl FnMut(Option<&CommandPath>)) {
        self.world_path.with(f);
    }

    pub fn build_dependencies(&self) {
        let shape = self
            .shape
            .get()
            .expect("shape should already be set in Path");
        let shape = shape.as_ref();

        shape
            .cast::<Component>()
            .push_dependent(self.as_object().cast());

        for path in shape.paths() {
            path.cast::<Component>()
                .as_ref()
                .push_dependent(self.as_object().cast());
        }
    }

    pub fn update(&self, value: FlagSet<ComponentDirt>) {
        if !Component::value_has_dirt(value, ComponentDirt::Path) {
            return;
        }

        let shape = self
            .shape
            .get()
            .expect("shape should already be set in Path");
        let shape = shape.as_ref();

        let space = shape.path_space();

        if space & PathSpace::Local == PathSpace::Local {
            let mut builder = CommandPathBuilder::new();

            let world = shape.cast::<TransformComponent>().world_transform();
            let inverse_world = world.invert().unwrap_or_default();

            for path in shape.paths() {
                let path = path.as_ref();

                let local_transform = inverse_world * path.transform();
                path.with_command_path(|command_path| {
                    builder.path(
                        command_path.expect("command_path should already be set"),
                        local_transform,
                    );
                });
            }

            self.local_path.set(Some(builder.build()));
        }

        if space & PathSpace::World == PathSpace::World {
            let mut builder = CommandPathBuilder::new();

            for path in shape.paths() {
                let path = path.as_ref();

                let transform = path.transform();
                path.with_command_path(|command_path| {
                    builder.path(
                        command_path.expect("command_path should already be set"),
                        transform,
                    );
                });
            }

            self.world_path.set(Some(builder.build()));
        }
    }
}

impl Core for PathComposer {
    parent_types![(component, Component)];

    properties!(component);
}

impl OnAdded for ObjectRef<'_, PathComposer> {
    on_added!([on_added_dirty], Component);

    fn on_added_clean(&self, _context: &dyn CoreContext) -> StatusCode {
        for parent in self.cast::<Component>().parents() {
            if let Some(shape) = parent.try_cast::<Shape>() {
                self.shape.set(Some(shape.clone()));
                shape.as_ref().set_path_composer(self.as_object());
                return StatusCode::Ok;
            }
        }

        StatusCode::MissingObject
    }
}
