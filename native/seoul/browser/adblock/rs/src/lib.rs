//! Narrow CXX boundary around Project Seoul's pinned adblock-rust engine.

use adblock::lists::FilterSet;
use adblock::request::Request;
use adblock::resources::Resource;
use adblock::Engine as InnerEngine;
use adblock::url_parser::ResolvesDomain;
use cxx::{let_cxx_string, CxxString, CxxVector};
use std::collections::HashSet;

struct Engine {
    inner: InnerEngine,
    main: InnerEngine,
    isolated_resources: Vec<Resource>,
    main_resources: Vec<Resource>,
}

#[cxx::bridge(namespace = "seoul::adblock_rs")]
mod ffi {
    enum BuildStatus {
        Success = 0,
        InvalidUtf8 = 1,
        InvalidResourceCatalog = 2,
    }

    struct OptionalString {
        has_value: bool,
        value: String,
    }

    struct MatchResult {
        matched: bool,
        important: bool,
        has_exception: bool,
        matched_rule: OptionalString,
        exception_rule: OptionalString,
        redirect: OptionalString,
        rewritten_url: OptionalString,
    }

    struct EngineBuildResult {
        value: Box<Engine>,
        status: BuildStatus,
        error_message: String,
    }

    struct CosmeticResources {
        hide_selectors: Vec<String>,
        exceptions: Vec<String>,
        isolated_script: String,
        main_world_script: String,
        procedural_actions: Vec<String>,
        generichide: bool,
    }

    struct DomainPosition {
        start: u32,
        end: u32,
    }

    unsafe extern "C++" {
        include!("seoul/browser/adblock/ad_block_domain_resolver.h");
        fn resolve_domain_position(host: &CxxString) -> DomainPosition;
    }

    extern "Rust" {
        type Engine;

        fn build_engine(
            rules: &CxxVector<u8>,
            resources_json: &CxxString,
        ) -> EngineBuildResult;
        fn matches(
            self: &Engine,
            url: &CxxString,
            hostname: &CxxString,
            source_hostname: &CxxString,
            request_type: &CxxString,
            third_party: bool,
            previously_matched_rule: bool,
            force_check_exceptions: bool,
        ) -> MatchResult;
        // Returns the combined `$csp` directive string for a document or
        // subdocument request, or an empty string when no policy applies. The
        // crate resolves `$csp` exceptions and merges multiple matching
        // directives itself; an empty result is always "inject nothing".
        fn csp_directives(
            self: &Engine,
            url: &CxxString,
            hostname: &CxxString,
            source_hostname: &CxxString,
            request_type: &CxxString,
            third_party: bool,
        ) -> String;
        fn url_cosmetic_resources(self: &Engine, url: &CxxString) -> CosmeticResources;
        fn hidden_class_id_selectors(
            self: &Engine,
            classes: &CxxVector<CxxString>,
            ids: &CxxVector<CxxString>,
            exceptions: &CxxVector<CxxString>,
        ) -> Vec<String>;
        fn serialize(self: &Engine) -> Vec<u8>;
        fn deserialize(self: &mut Engine, serialized: &CxxVector<u8>) -> bool;
    }
}

struct DomainResolver;

impl ResolvesDomain for DomainResolver {
    fn get_host_domain(&self, host: &str) -> (usize, usize) {
        let_cxx_string!(host_cxx = host);
        let position = ffi::resolve_domain_position(&host_cxx);
        (position.start as usize, position.end as usize)
    }
}

fn empty_engine() -> Box<Engine> {
    Box::new(Engine {
        inner: InnerEngine::default(),
        main: InnerEngine::default(),
        isolated_resources: Vec::new(),
        main_resources: Vec::new(),
    })
}

fn build_engine(
    rules: &CxxVector<u8>,
    resources_json: &CxxString,
) -> ffi::EngineBuildResult {
    let _ = adblock::url_parser::set_domain_resolver(Box::new(DomainResolver));
    let rules = match std::str::from_utf8(rules.as_slice()) {
        Ok(rules) => rules,
        Err(error) => {
            return ffi::EngineBuildResult {
                value: empty_engine(),
                status: ffi::BuildStatus::InvalidUtf8,
                error_message: error.to_string(),
            };
        }
    };
    let resources_json = match resources_json.to_str() {
        Ok(resources_json) => resources_json,
        Err(error) => {
            return ffi::EngineBuildResult {
                value: empty_engine(),
                status: ffi::BuildStatus::InvalidResourceCatalog,
                error_message: error.to_string(),
            };
        }
    };
    let mut resources = match serde_json::from_str::<Vec<Resource>>(resources_json) {
        Ok(resources) => resources,
        Err(error) => {
            return ffi::EngineBuildResult {
                value: empty_engine(),
                status: ffi::BuildStatus::InvalidResourceCatalog,
                error_message: error.to_string(),
            };
        }
    };

    // Resource implementations are compiled from a pinned pack. Remote rules can
    // select them and provide arguments, but cannot supply code or permissions.
    let upstream: Vec<serde_json::Value> = serde_json::from_str(include_str!(
        "../../../../third_party/ublock_scriptlets/resources.json"
    )).expect("verified bundled scriptlet descriptors");
    let reserved: HashSet<String> = resources.iter().flat_map(|r|
        std::iter::once(r.name.clone()).chain(r.aliases.iter().cloned())).collect();
    let mut main_resources = Vec::new();
    for value in upstream {
        let world = value["world"].as_str().unwrap_or("MAIN").to_owned();
        let resource: Resource = serde_json::from_value(value).expect("bundled resource schema");
        if world == "MAIN" || world == "BOTH" { main_resources.push(resource.clone()); }
        if (world == "ISOLATED" || world == "BOTH") &&
            !std::iter::once(&resource.name).chain(resource.aliases.iter()).any(|name| reserved.contains(name)) {
            resources.push(resource);
        }
    }
    let mut filter_set = FilterSet::new(true);
    filter_set.add_filter_list(rules, Default::default());
    let mut inner = InnerEngine::from_filter_set(filter_set, true);
    inner.use_resources(resources.clone());
    // Avoid a second network engine: this engine owns only page-world scriptlet
    // rules and their exceptions. Keep the engine's own parser/argument escaping.
    let mut main_filters = FilterSet::new(true);
    main_filters.add_filters(rules.lines().filter(|line|
        line.contains("##+js(") || line.contains("#@#+js(")), Default::default());
    let mut main = InnerEngine::from_filter_set(main_filters, true);
    main.use_resources(main_resources.clone());
    ffi::EngineBuildResult {
        value: Box::new(Engine { inner, main, isolated_resources: resources, main_resources }),
        status: ffi::BuildStatus::Success,
        error_message: String::new(),
    }
}

fn optional_string(value: Option<String>) -> ffi::OptionalString {
    match value {
        Some(value) => ffi::OptionalString {
            has_value: true,
            value,
        },
        None => ffi::OptionalString {
            has_value: false,
            value: String::new(),
        },
    }
}

fn empty_match_result() -> ffi::MatchResult {
    ffi::MatchResult {
        matched: false,
        important: false,
        has_exception: false,
        matched_rule: optional_string(None),
        exception_rule: optional_string(None),
        redirect: optional_string(None),
        rewritten_url: optional_string(None),
    }
}

fn empty_cosmetic_resources() -> ffi::CosmeticResources {
    ffi::CosmeticResources {
        hide_selectors: Vec::new(),
        exceptions: Vec::new(),
        isolated_script: String::new(),
        main_world_script: String::new(),
        procedural_actions: Vec::new(),
        generichide: false,
    }
}

impl Engine {
    fn matches(
        &self,
        url: &CxxString,
        hostname: &CxxString,
        source_hostname: &CxxString,
        request_type: &CxxString,
        third_party: bool,
        previously_matched_rule: bool,
        force_check_exceptions: bool,
    ) -> ffi::MatchResult {
        let Ok(url) = url.to_str() else {
            return empty_match_result();
        };
        let Ok(hostname) = hostname.to_str() else {
            return empty_match_result();
        };
        let Ok(source_hostname) = source_hostname.to_str() else {
            return empty_match_result();
        };
        let Ok(request_type) = request_type.to_str() else {
            return empty_match_result();
        };

        let request = Request::preparsed(url, hostname, source_hostname, request_type, third_party);
        let result = self.inner.check_network_request_subset(
            &request,
            previously_matched_rule,
            force_check_exceptions,
        );
        ffi::MatchResult {
            matched: result.matched,
            important: result.important,
            has_exception: result.exception.is_some(),
            matched_rule: optional_string(result.filter),
            exception_rule: optional_string(result.exception),
            redirect: optional_string(result.redirect),
            rewritten_url: optional_string(result.rewritten_url),
        }
    }

    fn csp_directives(
        &self,
        url: &CxxString,
        hostname: &CxxString,
        source_hostname: &CxxString,
        request_type: &CxxString,
        third_party: bool,
    ) -> String {
        let (Ok(url), Ok(hostname), Ok(source_hostname), Ok(request_type)) = (
            url.to_str(),
            hostname.to_str(),
            source_hostname.to_str(),
            request_type.to_str(),
        ) else {
            return String::new();
        };

        let request = Request::preparsed(url, hostname, source_hostname, request_type, third_party);
        self.inner.get_csp_directives(&request).unwrap_or_default()
    }

    fn url_cosmetic_resources(&self, url: &CxxString) -> ffi::CosmeticResources {
        let Ok(url) = url.to_str() else {
            return empty_cosmetic_resources();
        };
        let resources = self.inner.url_cosmetic_resources(url);
        let mut hide_selectors: Vec<String> = resources.hide_selectors.into_iter().collect();
        let mut exceptions: Vec<String> = resources.exceptions.into_iter().collect();
        let mut procedural_actions: Vec<String> =
            resources.procedural_actions.into_iter().collect();
        hide_selectors.sort_unstable();
        exceptions.sort_unstable();
        procedural_actions.sort_unstable();
        ffi::CosmeticResources {
            hide_selectors,
            exceptions,
            isolated_script: resources.injected_script,
            main_world_script: self.main.url_cosmetic_resources(url).injected_script,
            procedural_actions,
            generichide: resources.generichide,
        }
    }

    fn hidden_class_id_selectors(
        &self,
        classes: &CxxVector<CxxString>,
        ids: &CxxVector<CxxString>,
        exceptions: &CxxVector<CxxString>,
    ) -> Vec<String> {
        let classes = classes.iter().filter_map(|value| value.to_str().ok());
        let ids = ids.iter().filter_map(|value| value.to_str().ok());
        let exceptions: HashSet<String> = exceptions
            .iter()
            .filter_map(|value| value.to_str().ok().map(str::to_owned))
            .collect();
        let mut selectors = self
            .inner
            .hidden_class_id_selectors(classes, ids, &exceptions);
        selectors.sort_unstable();
        selectors
    }

    fn serialize(&self) -> Vec<u8> {
        let inner = self.inner.serialize();
        let main = self.main.serialize();
        let mut output = b"SEL2".to_vec();
        output.extend_from_slice(&(inner.len() as u64).to_le_bytes());
        output.extend(inner);
        output.extend(main);
        output
    }

    fn deserialize(&mut self, serialized: &CxxVector<u8>) -> bool {
        let bytes = serialized.as_slice();
        if bytes.len() < 12 || &bytes[..4] != b"SEL2" { return false; }
        let size = u64::from_le_bytes(bytes[4..12].try_into().unwrap());
        let Ok(size) = usize::try_from(size) else { return false; };
        let Some(end) = size.checked_add(12) else { return false; };
        if end > bytes.len() { return false; }
        let mut inner = InnerEngine::default();
        let mut main = InnerEngine::default();
        if inner.deserialize(&bytes[12..end]).is_err() || main.deserialize(&bytes[end..]).is_err() {
            return false;
        }
        inner.use_resources(self.isolated_resources.clone());
        main.use_resources(self.main_resources.clone());
        self.inner = inner;
        self.main = main;
        true
    }
}
