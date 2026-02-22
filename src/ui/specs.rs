// Render the static hardware specs view.

use eframe::egui;

use crate::specs::SystemSpecs;

pub fn render_specs(ui: &mut egui::Ui, specs: &SystemSpecs) {
    let rows: &[(&str, egui::Color32, &str, &str)] = &[
        (
            "\u{26A1}",
            egui::Color32::from_rgb(100, 160, 255),
            "CPU",
            &specs.cpu_name,
        ),
        (
            "\u{25A6}",
            egui::Color32::from_rgb(255, 175, 50),
            "Mainboard",
            &specs.mainboard,
        ),
        (
            "\u{2630}",
            egui::Color32::from_rgb(80, 210, 130),
            "Memory",
            &specs.memory_summary,
        ),
        (
            "\u{25C6}",
            egui::Color32::from_rgb(240, 90, 90),
            "Graphics",
            &specs.graphics,
        ),
        (
            "\u{25A3}",
            egui::Color32::from_rgb(180, 140, 240),
            "Display",
            &specs.display,
        ),
    ];

    ui.add_space(2.0);

    egui::Grid::new("specs_grid")
        .num_columns(3)
        .spacing(egui::vec2(8.0, 6.0))
        .show(ui, |ui| {
            for &(icon, color, label, value) in rows {
                ui.label(egui::RichText::new(icon).size(12.0).color(color));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.label(
                        egui::RichText::new(label)
                            .size(10.0)
                            .color(egui::Color32::from_gray(120)),
                    );
                });
                ui.label(egui::RichText::new(value).size(11.0).color(egui::Color32::WHITE));
                ui.end_row();
            }
        });
}
