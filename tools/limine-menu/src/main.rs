use limine_tool::limine::{Document, Node};
use serde_json::Value;
use std::io::{self, Read};

fn nodes(values: &Value, depth: usize, count: &mut usize) -> Result<Vec<Node>, String> {
    if depth > 16 { return Err("Menu nesting is too deep".into()); }
    let values = values.as_array().ok_or("Invalid menu entries")?;
    let mut result = Vec::new();
    for value in values {
        *count += 1;
        if *count > 2048 { return Err("Too many menu entries".into()); }
        let name = value["name"].as_str().ok_or("Missing entry name")?;
        if name.trim().is_empty() || name.chars().any(char::is_control) {
            return Err("Invalid entry name".into());
        }
        let mut lines = Vec::new();
        for line in value["lines"].as_array().ok_or("Missing entry configuration")? {
            let line = line.as_str().ok_or("Invalid entry configuration")?;
            if line.contains(['\n', '\r', '\0']) || line.trim_start().starts_with('/') {
                return Err("Invalid entry configuration".into());
            }
            lines.push(line.to_owned());
        }
        let children = nodes(&value["children"], depth + 1, count)?;
        let expanded = value["expanded"].as_bool().unwrap_or(false);
        let heading = value["heading"].as_str().filter(|_| value["originalName"].as_str() == Some(name));
        let title = heading.map(|line| line.trim_start_matches('/').trim_start_matches('+')).unwrap_or(name);
        result.push(Node { name: format!("{}{plus}{title}", "/".repeat(depth), plus = if expanded { "+" } else { "" }),
            clean_name: name.into(), config_lines: lines, children, line_break: false });
    }
    Ok(result)
}

fn decorate(values: &mut Value, nodes: &[Node]) {
    for (value, node) in values.as_array_mut().unwrap().iter_mut().zip(nodes) {
        value["expanded"] = node.name.trim().trim_start_matches('/').starts_with('+').into();
        value["heading"] = node.name.clone().into();
        value["originalName"] = node.clean_name.clone().into();
        decorate(&mut value["children"], &node.children);
    }
}

fn visible(values: &Value, output: &mut Vec<String>) {
    for value in values.as_array().unwrap() {
        let lines = value["lines"].as_array().unwrap();
        let skip = value["children"].as_array().unwrap().is_empty() && lines.iter().any(|line| {
            line.as_str().and_then(|line| line.split_once(':')).is_some_and(|(key, value)| {
                key.trim().eq_ignore_ascii_case("protocol") && matches!(value.trim(), "bios" | "bios_chainload")
            })
        });
        if skip { continue; }
        output.push(value["selection"].as_str().unwrap_or("").to_owned());
        if value["expanded"].as_bool().unwrap_or(false) { visible(&value["children"], output); }
    }
}

fn execute(request: Value) -> Result<String, String> {
    if request["action"].as_str() == Some("generator-path") {
        let config = limine_tool::config::AppConfig::load(&limine_tool::config::ConfigPaths::system()).map_err(|e| e.to_string())?;
        return Ok(serde_json::json!({"path": config.esp_path.join("limine.conf")}).to_string());
    }
    let content = request["content"].as_str().ok_or("Missing menu content")?;
    let mut document = Document::parse(content).map_err(|e| e.to_string())?;
    match request["action"].as_str() {
        Some("inspect") => {
            let mut menu: Value = serde_json::from_str(&document.tree_json()).map_err(|e| e.to_string())?;
            decorate(&mut menu["entries"], &document.root.children);
            Ok(menu.to_string())
        },
        Some("setting") => {
            let key = request["key"].as_str().ok_or("Missing setting name")?;
            let value = request["value"].as_str().ok_or("Missing setting value")?;
            if key.chars().any(char::is_control) || value.chars().any(char::is_control) {
                return Err("Invalid menu setting".into());
            }
            if value.is_empty() {
                document.root.config_lines.retain(|line| !line.split_once(':')
                    .is_some_and(|(name, _)| name.trim().eq_ignore_ascii_case(key)));
            } else { document.set_global_option(key, value); }
            Ok(serde_json::json!({"content": document.render()}).to_string())
        }
        Some("render") => {
            let mut old: Value = serde_json::from_str(&document.tree_json()).map_err(|e| e.to_string())?;
            decorate(&mut old["entries"], &document.root.children);
            let mut old_visible = Vec::new();
            visible(&old["entries"], &mut old_visible);
            let old_index = old["globals"]["default_entry"].as_str().unwrap_or("1").parse::<usize>().unwrap_or(1).saturating_sub(1);
            let mut new_visible = Vec::new();
            visible(&request["entries"], &mut new_visible);
            let selected = old_visible.get(old_index).and_then(|id| new_visible.iter().position(|value| value == id)).unwrap_or(0) + 1;
            document.root.children = nodes(&request["entries"], 1, &mut 0)?;
            if selected != old_index + 1 { document.set_global_option("default_entry", &selected.to_string()); }
            let rendered = document.render();
            Document::parse(&rendered).map_err(|e| e.to_string())?;
            Ok(serde_json::json!({"content": rendered}).to_string())
        }
        _ => Err("Invalid menu action".into()),
    }
}

fn main() {
    let mut input = String::new();
    let result = io::stdin().take(4 * 1024 * 1024).read_to_string(&mut input)
        .map_err(|e| e.to_string()).and_then(|_| serde_json::from_str(&input).map_err(|e| e.to_string()))
        .and_then(execute);
    match result {
        Ok(value) => println!("{value}"),
        Err(error) => { eprintln!("{error}"); std::process::exit(1); }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn edit_nested_menu_preserves_macros_and_unknown_lines() {
        let original = "${ROOT}=boot():\ntimeout: 5\n/Linux\n//Main\nprotocol: linux\npath: ${ROOT}/vmlinuz\nunknown_option: retained\n/Windows\nprotocol: efi\npath: boot():/EFI/windows.efi\n";
        let mut menu: Value = serde_json::from_str(&execute(serde_json::json!({"action":"inspect", "content":original})).unwrap()).unwrap();
        menu["entries"][0]["children"][0]["name"] = "Updated".into();
        let entries = menu["entries"].as_array_mut().unwrap();
        entries.swap(0, 1);
        let rendered: Value = serde_json::from_str(&execute(serde_json::json!({"action":"render", "content":original, "entries":entries})).unwrap()).unwrap();
        let text = rendered["content"].as_str().unwrap();
        assert!(text.contains("${ROOT}=boot():"));
        assert!(text.contains("unknown_option: retained"));
        assert!(text.find("/Windows").unwrap() < text.find("/Linux").unwrap());
        assert!(text.contains("//Updated"));
        assert!(text.contains("path: ${ROOT}/vmlinuz"));
    }
    #[test]
    fn preserve_expanded_macro_title_and_default_after_reorder() {
        let original = "${OS}=CatOS\ndefault_entry: 3\n/+${OS}\n//Main\nprotocol: linux\npath: boot():/vmlinuz\n/Windows\nprotocol: efi\npath: boot():/windows.efi\n";
        let mut menu: Value = serde_json::from_str(&execute(serde_json::json!({"action":"inspect", "content":original})).unwrap()).unwrap();
        assert_eq!(menu["entries"][0]["name"], "CatOS");
        assert_eq!(menu["entries"][0]["expanded"], true);
        let entries = menu["entries"].as_array_mut().unwrap();
        entries.swap(0, 1);
        let rendered: Value = serde_json::from_str(&execute(serde_json::json!({"action":"render", "content":original, "entries":entries})).unwrap()).unwrap();
        let text = rendered["content"].as_str().unwrap();
        assert!(text.contains("/+${OS}"));
        assert!(text.contains("default_entry: 1"));
        assert!(text.find("/Windows").unwrap() < text.find("/+${OS}").unwrap());
    }

}
