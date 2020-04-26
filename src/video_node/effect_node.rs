use crate::err::Result;
use crate::graphics::{Fbo, RenderChain, Shader};
use crate::resources;
use crate::video_node::{VideoNode, VideoNodeId, VideoNodeKind, VideoNodeKindMut};

use log::*;
use std::borrow::Borrow;
use std::collections::HashMap;
use std::rc::Rc;
use web_sys::WebGlRenderingContext as GL;

pub struct EffectNode {
    id: VideoNodeId,
    name: String,
    n_inputs: usize,
    time: f64,
    intensity: f64,
    intensity_integral: f64,
    shader_sources: Vec<String>,
    shader_passes: Vec<Option<Shader>>,
    properties: HashMap<String, String>,
}

impl EffectNode {
    pub fn new(name: &str) -> Result<EffectNode> {
        let program = resources::effects::lookup(name).ok_or("Unknown effect name")?;

        let header_source = String::from(resources::glsl::EFFECT_HEADER);
        let mut source = String::new();
        let mut properties = HashMap::new();

        source.push_str(&header_source);
        source.push_str("\n#line 1\n");

        let mut shader_sources = Vec::new();
        for (i, line) in program.split('\n').enumerate() {
            let mut terms = line.trim().splitn(3, ' ');
            let head = terms.next();
            match head {
                Some("#property") => {
                    let key = terms
                        .next()
                        .ok_or("Parse error in #property line")?
                        .to_string();
                    let value = terms
                        .next()
                        .ok_or("Parse error in #property line")?
                        .to_string();
                    properties.insert(key, value);
                }
                Some("#buffershader") => {
                    shader_sources.push(source);
                    source = String::new();
                    source.push_str(&header_source);
                    source.push_str(&format!("\n#line {}\n", i + 1));
                }
                _ => {
                    source.push_str(&line);
                }
            }
            source.push_str("\n");
        }
        shader_sources.push(source);

        let shader_passes = shader_sources.iter().map(|_| None).collect();

        let n_inputs: usize = properties
            .get("inputCount")
            .map_or(Ok(1), |x| x.parse().map_err(|_| "Invalid inputCount"))?;

        info!("Loaded effect: {:?}", name);

        let id = VideoNodeId::new();
        Ok(EffectNode {
            id,
            name: String::from(name),
            n_inputs,
            time: 0.0,
            intensity: 0.0,
            intensity_integral: 0.0,
            shader_sources,
            shader_passes,
            properties,
        })
    }

    pub fn set_intensity(&mut self, intensity: f64) {
        self.intensity = intensity;
    }

    pub fn intensity(&self) -> f64 {
        self.intensity
    }
}

impl VideoNode for EffectNode {
    fn id(&self) -> VideoNodeId {
        self.id
    }

    fn name(&self) -> &str {
        &self.name
    }

    fn n_inputs(&self) -> usize {
        self.n_inputs
    }

    fn n_buffers(&self) -> usize {
        self.shader_passes.len()
    }

    fn pre_render(&mut self, chain: &RenderChain, time: f64) {
        let dt = time - self.time;
        self.intensity_integral = (self.intensity_integral + dt * self.intensity) % 1024.0;
        self.time = time;

        for (src, shader) in self
            .shader_sources
            .iter()
            .zip(self.shader_passes.iter_mut())
        {
            if shader.is_none() {
                if let Ok(new_shader) = chain.compile_fragment_shader(src) {
                    shader.replace(new_shader);
                }
            }
        }
    }

    fn render<'a>(
        &'a self,
        chain: &'a RenderChain,
        input_fbos: &[Option<Rc<Fbo>>],
        buffer_fbos: &mut [Rc<Fbo>],
    ) -> Option<Rc<Fbo>> {
        assert!(input_fbos.len() == self.n_inputs());
        assert!(buffer_fbos.len() == self.n_buffers());
        for (i, shader) in self.shader_passes.iter().enumerate().rev() {
            let shader: &Shader = shader.as_ref().unwrap_or(&chain.blit_shader);

            let active_shader = shader.begin_render(chain, Some(&chain.extra_fbo.borrow()));
            let mut tex_index: u32 = 0;

            chain.context.active_texture(GL::TEXTURE0 + tex_index);
            chain
                .context
                .bind_texture(GL::TEXTURE_2D, Some(&chain.noise_texture));
            let loc = active_shader.get_uniform_location("iNoise");
            chain.context.uniform1i(loc.as_ref(), tex_index as i32);
            tex_index += 1;

            let mut inputs: Vec<i32> = vec![];
            for fbo in input_fbos {
                chain.bind_fbo_to_texture(
                    GL::TEXTURE0 + tex_index,
                    fbo.as_ref().map(|x| x.borrow()),
                );
                inputs.push(tex_index as i32);
                tex_index += 1;
            }
            let loc = active_shader.get_uniform_location("iInputs");
            chain
                .context
                .uniform1iv_with_i32_array(loc.as_ref(), &inputs);

            let mut channels: Vec<i32> = vec![];
            for fbo in buffer_fbos.iter() {
                chain.bind_fbo_to_texture(GL::TEXTURE0 + tex_index, Some(fbo));
                channels.push(tex_index as i32);
                tex_index += 1;
            }
            let loc = active_shader.get_uniform_location("iChannel");
            chain
                .context
                .uniform1iv_with_i32_array(loc.as_ref(), &channels);

            let loc = active_shader.get_uniform_location("iIntensity");
            chain.context.uniform1f(loc.as_ref(), self.intensity as f32);

            let loc = active_shader.get_uniform_location("iIntensityIntegral");
            chain
                .context
                .uniform1f(loc.as_ref(), self.intensity_integral as f32);

            let loc = active_shader.get_uniform_location("iTime");
            chain
                .context
                .uniform1f(loc.as_ref(), (self.time % 2048.) as f32);

            let loc = active_shader.get_uniform_location("iStep");
            chain
                .context
                .uniform1f(loc.as_ref(), (self.time % 2048.) as f32);

            let loc = active_shader.get_uniform_location("iFPS");
            chain.context.uniform1f(loc.as_ref(), 60.);

            let loc = active_shader.get_uniform_location("iAudio");
            chain
                .context
                .uniform4fv_with_f32_array(loc.as_ref(), &[0.1, 0.2, 0.3, 0.4]);

            let loc = active_shader.get_uniform_location("iResolution");
            chain
                .context
                .uniform2f(loc.as_ref(), chain.size.0 as f32, chain.size.1 as f32);

            active_shader.finish_render();
            std::mem::swap(&mut *chain.extra_fbo.borrow_mut(), &mut buffer_fbos[i]);
        }
        Some(Rc::clone(&buffer_fbos.first().unwrap()))
    }

    fn downcast(&self) -> Option<VideoNodeKind> {
        Some(VideoNodeKind::Effect(self))
    }
    fn downcast_mut(&mut self) -> Option<VideoNodeKindMut> {
        Some(VideoNodeKindMut::Effect(self))
    }
}
