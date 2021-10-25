use flagset::{flags, FlagSet};

flags! {
    pub enum ComponentDirt: u16 {
        Dependents,
        /// General flag for components are dirty (if this is up, the update
        /// cycle runs). It gets automatically applied with any other dirt.
        Components,
        /// Draw order needs to be re-computed.
        DrawOrder,
        /// Path is dirty and needs to be rebuilt.
        Path,
        /// Vertices have changed, re-order cached lists.
        Vertices,
        /// Used by any component that needs to recompute their local transform.
        /// Usually components that have their transform dirty will also have
        /// their worldTransform dirty.
        Transform,
        /// Used by any component that needs to update its world transform.
        WorldTransform,
        /// Marked when the stored render opacity needs to be updated.
        RenderOpacity,
        /// Dirt used to mark some stored paint needs to be rebuilt or that we
        /// just want to trigger an update cycle so painting occurs.
        Paint,
        /// Used by the gradients track when the stops need to be re-ordered.
        Stops,
    }
}
