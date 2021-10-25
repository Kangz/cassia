use crate::{
    core::{Core, Object, ObjectRef, OnAdded, Property},
    draw_rules::DrawRules,
    dyn_vec::DynVec,
    math::Mat,
    node::Node,
    option_cell::OptionCell,
    shapes::{paint::BlendMode, ClippingShape, Shape},
    Renderer,
};

#[derive(Debug)]
pub struct Drawable {
    node: Node,
    blend_mode: Property<BlendMode>,
    clipping_shapes: DynVec<Object<ClippingShape>>,
    pub(crate) flattened_draw_rules: OptionCell<Object<DrawRules>>,
    pub(crate) prev: OptionCell<Object<Self>>,
    pub(crate) next: OptionCell<Object<Self>>,
}

impl ObjectRef<'_, Drawable> {
    pub fn blend_mode(&self) -> BlendMode {
        self.blend_mode.get()
    }

    pub fn set_blend_mode(&self, blend_mode: BlendMode) {
        self.blend_mode.set(blend_mode);
    }
}

impl ObjectRef<'_, Drawable> {
    pub fn push_clipping_shape(&self, clipping_shape: Object<ClippingShape>) {
        self.clipping_shapes.push(clipping_shape);
    }

    pub fn clip(&self) {
        todo!();
    }

    pub fn draw(&self, renderer: &mut impl Renderer, transform: Mat) {
        if let Some(shape) = self.try_cast::<Shape>() {
            return shape.draw(renderer, transform);
        }

        unreachable!()
    }
}

impl Core for Drawable {
    parent_types![(node, Node)];

    properties![(23, blend_mode, set_blend_mode), node];
}

impl OnAdded for ObjectRef<'_, Drawable> {
    on_added!(Node);
}

impl Default for Drawable {
    fn default() -> Self {
        Self {
            node: Node::default(),
            blend_mode: Property::new(BlendMode::SrcOver),
            clipping_shapes: DynVec::new(),
            flattened_draw_rules: OptionCell::new(),
            prev: OptionCell::new(),
            next: OptionCell::new(),
        }
    }
}
