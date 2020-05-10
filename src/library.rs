use crate::err::{Error, Result};

use log::*;
use serde::{Deserialize, Serialize};
use serde_json::Value as JsonValue;
use std::cell::RefCell;
use std::collections::hash_map::DefaultHasher;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::rc::Rc;
use wasm_bindgen::JsCast;
use wasm_bindgen_futures::{spawn_local, JsFuture};
use web_sys::{Request, RequestInit, RequestMode, Response};

#[derive(Clone)]
pub struct Library {
    library_ref: Rc<RefCell<LibraryRef>>,
}

struct LibraryRef {
    items: HashMap<String, Status<Entry>>,
    content: Content,
}

#[serde(rename_all = "camelCase", tag = "type")]
#[derive(Clone, Serialize)]
enum Entry {
    #[serde(rename_all = "camelCase")]
    Effect { source_hash: ContentHash },
}

#[derive(Clone)]
#[serde(rename_all = "camelCase", tag = "status")]
#[derive(Serialize)]
pub enum Status<T> {
    Pending,
    Loaded(T),
    Failed { error: String },
    Invalid,
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct ContentHash(String);

/// Content-Addressable Storage
#[derive(Debug, Default)]
struct Content {
    map: HashMap<ContentHash, String>,
}

async fn fetch_url(url: &str) -> Result<String> {
    let mut opts = RequestInit::new();
    opts.method("GET");
    opts.mode(RequestMode::Cors);

    let request = Request::new_with_str_and_init(&url, &opts)?;

    let window = web_sys::window().unwrap();
    let resp_value = JsFuture::from(window.fetch_with_request(&request)).await?;

    // `resp_value` is a `Response` object.
    assert!(resp_value.is_instance_of::<Response>());
    let resp: Response = resp_value.dyn_into().unwrap();

    if !resp.ok() {
        return Err(Error::Http(resp.status_text()));
    }

    // Convert this other `Promise` into a rust `Future`.
    let text = JsFuture::from(resp.text()?).await?.as_string().unwrap();

    Ok(text)
}

impl Library {
    pub fn new() -> Library {
        let library_ref = LibraryRef {
            items: Default::default(),
            content: Default::default(),
        };

        Library {
            library_ref: Rc::new(RefCell::new(library_ref)),
        }
    }

    pub fn load_all(&self) {
        let lib = Rc::clone(&self.library_ref);
        spawn_local(async move {
            match fetch_url("./effect_list.txt").await {
                Ok(list_text) => {
                    let futs = list_text
                        .lines()
                        .map(|name| Self::load_effect(Rc::clone(&lib), name));
                    futures::future::join_all(futs).await;
                },
                Err(e) => {
                    error!("Unable to fetch effects_list; no effects will load ({})", e);
                }
            }
        });
    }

    async fn load_effect(lib: Rc<RefCell<LibraryRef>>, effect_name: &str) {
        lib.borrow_mut()
            .items
            .insert(effect_name.to_string(), Status::Pending);
        async move {
            let url = format!("./effects/{}.glsl", effect_name);
            let effect_name = effect_name.to_string();
            match fetch_url(&url).await {
                Ok(source) => {
                    lib.borrow_mut().set_effect_source(effect_name, source);
                }
                Err(e) => {
                    lib.borrow_mut().items.insert(
                        effect_name,
                        Status::Failed {
                            error: e.to_string(),
                        },
                    );
                }
            }
        }
        .await
    }

    pub fn effect_source(&self, effect_name: &str) -> Status<String> {
        let lib = self.library_ref.borrow();
        let status = (*lib)
            .items
            .get(effect_name)
            .cloned()
            .unwrap_or(Status::Invalid);
        match status {
            Status::Loaded(Entry::Effect { ref source_hash }) => {
                if let Some(source) = lib.content.get(source_hash) {
                    Status::Loaded(source.clone())
                } else {
                    Status::Failed {
                        error: format!("Invalid hash: {:?}", source_hash),
                    }
                }
            }
            Status::Pending => Status::Pending,
            Status::Failed { error } => Status::Failed { error },
            Status::Invalid => Status::Invalid,
        }
    }

    pub fn set_effect_source(&self, name: String, source: String) {
        self.library_ref
            .borrow_mut()
            .set_effect_source(name, source)
    }

    pub fn items(&self) -> JsonValue {
        let lib = self.library_ref.borrow();
        //serde_json::to_value(&lib.items).unwrap_or(JsonValue::Null)
        serde_json::to_value(&lib.items).unwrap()
    }

    pub fn content(&self, hash: &ContentHash) -> Option<String> {
        let lib = self.library_ref.borrow();
        lib.content.get(hash).cloned()
    }
}

impl LibraryRef {
    fn set_effect_source(&mut self, name: String, source: String) {
        let hash = self.content.insert(source);
        let entry = Entry::Effect { source_hash: hash };
        self.items.insert(name, Status::Loaded(entry));
    }
}

impl Content {
    fn get(&self, hash: &ContentHash) -> Option<&String> {
        self.map.get(hash)
    }

    fn insert(&mut self, content: String) -> ContentHash {
        let hash = Self::hash(&content);
        self.map
            .entry(hash.clone())
            .and_modify(|e| assert_eq!(e, &content))
            .or_insert(content);
        hash
    }

    fn hash(content: &str) -> ContentHash {
        // TODO: Replace this with a more robust hash function
        let mut s = DefaultHasher::new();
        content.hash(&mut s);
        let h = s.finish();
        ContentHash(h.to_string())
    }
}