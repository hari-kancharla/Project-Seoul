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
    let resources = match serde_json::from_str::<Vec<Resource>>(resources_json) {
        Ok(resources) => resources,
        Err(error) => {
            return ffi::EngineBuildResult {
                value: empty_engine(),
                status: ffi::BuildStatus::InvalidResourceCatalog,
                error_message: error.to_string(),
            };
        }
    };

    let mut filter_set = FilterSet::new(true);
    filter_set.add_filter_list(rules, Default::default());
    let mut inner = InnerEngine::from_filter_set(filter_set, true);
    inner.use_resources(resources);
    ffi::EngineBuildResult {
        value: Box::new(Engine { inner }),
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
        self.inner.serialize()
    }

    fn deserialize(&mut self, serialized: &CxxVector<u8>) -> bool {
        self.inner.deserialize(serialized.as_slice()).is_ok()
    }
}
