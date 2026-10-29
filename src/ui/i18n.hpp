#pragma once

#include <optional>
#include <string_view>

namespace instaseek::ui {

enum class Language { English, Russian };

inline Language& activeLanguage() {
    static Language language = Language::English;
    return language;
}

inline std::optional<Language> parseLanguage(std::string_view code) {
    if (code == "en") return Language::English;
    if (code == "ru") return Language::Russian;
    return std::nullopt;
}

inline Language otherLanguage(Language language) {
    return language == Language::English ? Language::Russian : Language::English;
}

inline char thousandsSeparator() { return activeLanguage() == Language::Russian ? ' ' : ','; }

struct Text {
    const char* en;
    const char* ru;
};

inline const char* tr(const Text& text) { return activeLanguage() == Language::Russian ? text.ru : text.en; }

namespace strings {

inline constexpr Text app_name{"InstaSeek", "InstaSeek"};
inline constexpr Text tagline{"instant file & content search for Windows", "мгновенный поиск файлов и текста для Windows"};
inline constexpr Text splash_loading{"warming up", "разогреваемся"};
inline constexpr Text splash_skip{"press any key to skip", "нажмите любую клавишу"};

inline constexpr Text tab_files{"Files", "Файлы"};
inline constexpr Text tab_content{"Content", "Содержимое"};
inline constexpr Text tab_viewer{"Viewer", "Просмотр"};

inline constexpr Text placeholder_files{"Search file names, e.g.  report ext:pdf size:>10mb",
                                        "Поиск по именам, например:  отчёт ext:pdf size:>10mb"};
inline constexpr Text placeholder_pattern{"Text to find, a|b for any of them, or a regular expression",
                                          "Что искать: текст, a|b для любого из них или регулярное выражение"};
inline constexpr Text placeholder_location{"File or folder to search in, e.g.  D:\\logs",
                                           "Файл или папка для поиска, например:  D:\\logs"};
inline constexpr Text placeholder_open_file{"Path to a file, then Enter", "Путь к файлу, затем Enter"};
inline constexpr Text placeholder_goto{"line number, 0x1F00 for a byte offset, 50% for a position",
                                       "номер строки, 0x1F00 для смещения, 50% для позиции"};
inline constexpr Text placeholder_find{"text to find, Enter for next", "что найти, Enter для следующего"};

inline constexpr Text label_query{"Query", "Запрос"};
inline constexpr Text label_pattern{"Find", "Найти"};
inline constexpr Text label_location{"In", "Где"};
inline constexpr Text label_file{"File", "Файл"};
inline constexpr Text label_results{"Results", "Результаты"};
inline constexpr Text label_document{"Document", "Документ"};
inline constexpr Text label_goto{"Go to", "Перейти"};
inline constexpr Text label_find_in_file{"Find", "Найти"};

inline constexpr Text column_name{"Name", "Имя"};
inline constexpr Text column_folder{"Folder", "Папка"};
inline constexpr Text column_size{"Size", "Размер"};
inline constexpr Text column_modified{"Modified", "Изменён"};
inline constexpr Text column_file{"File", "Файл"};
inline constexpr Text column_line{"Line", "Строка"};
inline constexpr Text column_text{"Text", "Текст"};

inline constexpr Text option_case{"Aa case", "Aa регистр"};
inline constexpr Text option_word{"word", "слово"};
inline constexpr Text option_regex{".* regex", ".* regex"};
inline constexpr Text option_binary{"skip binary", "без бинарных"};
inline constexpr Text option_encoding{"encoding", "кодировка"};
inline constexpr Text option_auto{"auto", "авто"};

inline constexpr Text empty_files_title{"Type to search across the whole disk", "Начните вводить: поиск по всему диску"};
inline constexpr Text empty_files_hint{"a b = AND   a|b = OR   ext:pdf   size:>100mb   modified:>2024-01-01   type:folder   !exclude",
                                       "a b = И   a|b = ИЛИ   ext:pdf   size:>100mb   modified:>2024-01-01   type:folder   !исключить"};
inline constexpr Text files_no_results{"Nothing found", "Ничего не найдено"};
inline constexpr Text files_no_results_hint{"check the spelling or remove a filter", "проверьте написание или уберите фильтр"};
inline constexpr Text files_index_building{"the index is still being built, results will update",
                                           "индекс ещё строится, результаты обновятся"};
inline constexpr Text files_metadata_pending{"sizes and dates are still loading, size and date filters may miss files",
                                             "размеры и даты ещё загружаются, фильтры по ним могут пропустить файлы"};
inline constexpr Text files_invalid_filter{"not understood", "не понял"};
inline constexpr Text results_word{"results", "результатов"};
inline constexpr Text results_shown{"showing first", "показаны первые"};

inline constexpr Text empty_content_title{"Search text inside huge files, no indexing needed",
                                          "Поиск текста в огромных файлах без индексации"};
inline constexpr Text empty_content_hint{"error|warning = any of the words   whole word or RE2 regex   Browse... picks a file or folder   Enter to start",
                                         "error|warning = любое из слов   целое слово или RE2   Обзор... выбирает файл или папку   Enter для старта"};
inline constexpr Text content_need_pattern{"Type what to find", "Введите, что искать"};
inline constexpr Text content_need_location{"Choose a file or a folder", "Укажите файл или папку"};
inline constexpr Text content_location_missing{"Path not found", "Путь не найден"};
inline constexpr Text content_nothing_to_search{"No files to search there", "Там нет файлов для поиска"};
inline constexpr Text content_invalid_regex{"Invalid regular expression", "Неверное регулярное выражение"};
inline constexpr Text content_multiline{"The pattern must be a single line", "Шаблон должен быть в одну строку"};
inline constexpr Text content_searching{"searching", "ищу"};
inline constexpr Text content_hits{"hits", "совпадений"};
inline constexpr Text content_in{"in", "в"};
inline constexpr Text content_files{"files", "файлах"};
inline constexpr Text content_cancelled{"cancelled", "отменено"};
inline constexpr Text content_limit{"limit reached", "достигнут лимит"};
inline constexpr Text content_no_hits{"No matches", "Совпадений нет"};
inline constexpr Text content_skipped{"skipped", "пропущено"};
inline constexpr Text content_scanned{"scanned", "просмотрено"};

inline constexpr Text empty_viewer_title{"Open multi-gigabyte files instantly", "Мгновенно открывает файлы на гигабайты"};
inline constexpr Text empty_viewer_hint{"paste a path with Ctrl+V and press Enter, or open a search result",
                                        "вставьте путь (Ctrl+V) и нажмите Enter или откройте результат поиска"};
inline constexpr Text viewer_open_failed{"Cannot open the file", "Не удалось открыть файл"};
inline constexpr Text viewer_lines{"lines", "строк"};
inline constexpr Text viewer_indexing{"counting lines", "считаю строки"};
inline constexpr Text viewer_line{"Ln", "Стр"};
inline constexpr Text viewer_offset{"offset", "смещение"};
inline constexpr Text viewer_binary{"binary file", "бинарный файл"};
inline constexpr Text viewer_searching{"searching", "ищу"};
inline constexpr Text viewer_not_found{"No more matches", "Больше совпадений нет"};
inline constexpr Text viewer_goto_pending{"waiting for the line index", "жду подсчёта строк"};
inline constexpr Text viewer_bad_goto{"Use a line number, 0x offset or a percentage", "Нужен номер строки, 0x-смещение или процент"};
inline constexpr Text viewer_empty_file{"The file is empty", "Файл пуст"};
inline constexpr Text viewer_open_first{"Open a file first", "Сначала откройте файл"};
inline constexpr Text viewer_find_empty{"Type what to find in the Find field", "Введите, что искать, в поле «Найти»"};
inline constexpr Text viewer_match_found{"match found", "совпадение найдено"};
inline constexpr Text viewer_match_at{"match on line", "совпадение на строке"};
inline constexpr Text empty_viewer_keys{"Ctrl+F find in the file   F3 / Shift+F3 next / previous match   g go to line",
                                        "Ctrl+F поиск в файле   F3 / Shift+F3 следующее / предыдущее   g перейти к строке"};

inline constexpr Text status_volume{"volume", "том"};
inline constexpr Text status_index{"index", "индекс"};
inline constexpr Text status_files{"files", "файлов"};
inline constexpr Text status_last_search{"last search", "посл. поиск"};
inline constexpr Text status_admin{"admin", "админ"};
inline constexpr Text status_user{"standard user", "обычный пользователь"};
inline constexpr Text index_ntfs{"NTFS MFT", "NTFS MFT"};
inline constexpr Text index_scan{"directory scan", "обход каталогов"};
inline constexpr Text index_starting{"starting", "запуск"};
inline constexpr Text index_scanning{"indexing", "индексация"};
inline constexpr Text index_metadata{"reading sizes", "читаю размеры"};
inline constexpr Text index_live{"live", "live"};
inline constexpr Text index_failed{"index failed", "ошибка индекса"};

inline constexpr Text banner_elevation{"Fast NTFS indexing needs administrator rights: InstaSeek reads the MFT and USN journal (read-only). A slower directory scan is used now.",
                                       "Для быстрой индексации NTFS нужны права администратора: InstaSeek читает MFT и USN-журнал (только чтение). Сейчас используется медленный обход каталогов."};
inline constexpr Text banner_elevation_key{"restart as admin", "перезапуск с правами админа"};
inline constexpr Text banner_not_ntfs{"This volume is not NTFS, so a directory scan is used.",
                                      "Этот том не NTFS, поэтому используется обход каталогов."};

inline constexpr Text toast_copied{"Copied to the clipboard", "Скопировано в буфер обмена"};
inline constexpr Text toast_copied_path{"Path copied", "Путь скопирован"};
inline constexpr Text toast_copied_line{"Line copied", "Строка скопирована"};
inline constexpr Text toast_copy_failed{"Could not access the clipboard", "Не удалось открыть буфер обмена"};
inline constexpr Text toast_explorer_failed{"Could not open Explorer", "Не удалось открыть проводник"};

inline constexpr Text menu_paste{"Paste", "Вставить"};
inline constexpr Text menu_copy{"Copy", "Копировать"};
inline constexpr Text menu_cut{"Cut", "Вырезать"};
inline constexpr Text menu_select_all{"Select all", "Выделить всё"};
inline constexpr Text menu_clear{"Clear", "Очистить"};
inline constexpr Text menu_open_viewer{"Open in Viewer", "Открыть в просмотре"};
inline constexpr Text menu_open_at_line{"Open at this line", "Открыть на этой строке"};
inline constexpr Text menu_find_in_file{"Find in this file", "Найти в этом файле"};
inline constexpr Text menu_search_in_parent{"Search text in its folder", "Искать текст в его папке"};
inline constexpr Text menu_search_in_folder{"Search text in this folder", "Искать текст в этой папке"};
inline constexpr Text menu_copy_path{"Copy full path", "Копировать полный путь"};
inline constexpr Text menu_copy_name{"Copy name", "Копировать имя"};
inline constexpr Text menu_copy_folder{"Copy folder path", "Копировать путь к папке"};
inline constexpr Text menu_copy_line{"Copy line text", "Копировать текст строки"};
inline constexpr Text menu_copy_location{"Copy file:line", "Копировать файл:строка"};
inline constexpr Text menu_show_explorer{"Show in Explorer", "Показать в проводнике"};
inline constexpr Text menu_open_explorer{"Open in Explorer", "Открыть в проводнике"};
inline constexpr Text menu_pick_file{"Choose a file...", "Выбрать файл..."};
inline constexpr Text menu_pick_folder{"Choose a folder...", "Выбрать папку..."};
inline constexpr Text menu_find{"Find in file", "Найти в файле"};
inline constexpr Text menu_goto{"Go to line", "Перейти к строке"};
inline constexpr Text button_browse{"Browse...", "Обзор..."};
inline constexpr Text toast_language{"English interface", "Русский интерфейс"};
inline constexpr Text toast_indexed{"Indexed", "Проиндексировано"};
inline constexpr Text toast_files_in{"files in", "файлов за"};
inline constexpr Text toast_elevation_declined{"Elevation was cancelled", "Повышение прав отменено"};
inline constexpr Text toast_elevation_failed{"Could not restart as administrator", "Не удалось перезапуститься от администратора"};
inline constexpr Text toast_rescan{"Rebuilding the index", "Перестраиваю индекс"};
inline constexpr Text toast_search_cancelled{"Search cancelled", "Поиск отменён"};

inline constexpr Text hint_help{"help", "справка"};
inline constexpr Text hint_language{"русский", "english"};
inline constexpr Text hint_paste{"paste", "вставить"};
inline constexpr Text hint_menu{"menu: copy, open, Explorer", "меню: копировать, открыть, проводник"};
inline constexpr Text hint_browse{"browse", "обзор"};
inline constexpr Text key_right_click{"Right click", "ПКМ"};
inline constexpr Text hint_find_in_file{"find in file", "найти в файле"};
inline constexpr Text hint_modes{"modes", "режимы"};
inline constexpr Text hint_quit{"quit", "выход"};
inline constexpr Text hint_open{"open", "открыть"};
inline constexpr Text hint_select{"select", "выбор"};
inline constexpr Text hint_search{"search", "искать"};
inline constexpr Text hint_cancel{"cancel", "отмена"};
inline constexpr Text hint_options{"options", "опции"};
inline constexpr Text hint_toggle{"toggle", "вкл/выкл"};
inline constexpr Text hint_goto{"go to", "перейти"};
inline constexpr Text hint_find{"find", "найти"};
inline constexpr Text hint_next{"next/prev", "след./пред."};
inline constexpr Text hint_scroll{"scroll", "прокрутка"};
inline constexpr Text hint_back{"back", "назад"};
inline constexpr Text hint_rescan{"rescan", "переиндексация"};
inline constexpr Text hint_focus{"focus", "фокус"};

inline constexpr Text help_title{"Keyboard shortcuts", "Горячие клавиши"};
inline constexpr Text help_close{"Esc or F1 to close", "Esc или F1, чтобы закрыть"};
inline constexpr Text help_files_mode{"Files: search names on the whole disk", "Файлы: поиск имён по всему диску"};
inline constexpr Text help_content_mode{"Content: search text inside files", "Содержимое: поиск текста в файлах"};
inline constexpr Text help_viewer_mode{"Viewer: browse huge files", "Просмотр: просмотр огромных файлов"};
inline constexpr Text help_navigate{"Move through results", "Перемещение по результатам"};
inline constexpr Text help_open_result{"Open the result: file in Viewer, folder in Content", "Открыть результат: файл в просмотре, папку в поиске"};
inline constexpr Text help_next_field{"Next field or option, Space toggles an option", "Следующее поле или опция, пробел переключает"};
inline constexpr Text help_find_in_file{"Viewer: find in the file, F3 next match, Shift+F3 previous",
                                        "Просмотр: поиск в файле, F3 следующее совпадение, Shift+F3 предыдущее"};
inline constexpr Text help_clipboard{"Right click a result, a line or a field: copy, paste, open, show in Explorer",
                                     "Правый клик по результату, строке или полю: копировать, вставить, открыть, показать в проводнике"};
inline constexpr Text help_browse{"Pick a file or a folder in the Windows dialog", "Выбрать файл или папку в окне Windows"};
inline constexpr Text help_select_text{"Select text in a field, also Ctrl+A, mouse drag or double click",
                                       "Выделить текст в поле, также Ctrl+A, перетаскивание мышью или двойной клик"};
inline constexpr Text help_toggle_language{"Switch English / Russian", "Английский / русский"};
inline constexpr Text help_show_help{"Show or hide this help", "Показать или скрыть справку"};
inline constexpr Text help_escape{"Close a dialog, cancel a search, leave the document", "Закрыть окно, отменить поиск, выйти из документа"};
inline constexpr Text help_rescan{"Rebuild the file index", "Перестроить индекс файлов"};
inline constexpr Text help_elevate{"Restart as administrator for NTFS indexing", "Перезапуск от администратора для индексации NTFS"};
inline constexpr Text help_quit{"Quit", "Выход"};
inline constexpr Text help_viewer_keys{"Viewer: g or Ctrl+G go to line, n / N next / previous match",
                                       "Просмотр: g или Ctrl+G переход к строке, n / N следующее / предыдущее совпадение"};
inline constexpr Text help_filters{"Filters: ext:pdf,docx  size:>10mb  modified:today  type:folder  path:docs  !exclude",
                                   "Фильтры: ext:pdf,docx  size:>10mb  modified:today  type:folder  path:docs  !исключить"};
inline constexpr Text help_mouse{"Mouse: click tabs and rows, double click opens, wheel scrolls, Shift+drag selects screen text in Windows Terminal",
                                 "Мышь: клик по вкладкам и строкам, двойной клик открывает, колесо прокручивает, Shift+перетаскивание выделяет текст экрана в Windows Terminal"};
inline constexpr Text help_layout_note{"Letter shortcuts work in both the Latin and the Russian layout.",
                                       "Буквенные сочетания работают и в латинской, и в русской раскладке."};

inline constexpr Text terminal_too_small{"The terminal window is too small", "Окно терминала слишком маленькое"};
inline constexpr Text terminal_needed_size{"InstaSeek needs at least", "InstaSeek нужно не меньше"};

}

}
