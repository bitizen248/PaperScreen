#include <string.h>
#ifndef UNIT_TEST
#include <esp_log.h>
#include <esp_system.h>
#else
#define ESP_LOGI(args...)
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#define ESP_LOGD(args...)
#endif
#include "EpubReader.h"
#include "Epub.h"
#include "../RubbishHtmlParser/RubbishHtmlParser.h"
#include "../Renderer/Renderer.h"
#include "CrashCheckpoint.h"

static const char *TAG = "EREADER";

bool EpubReader::load()
{
  SET_CHECKPOINT(20);
  ESP_LOGD(TAG, "Before epub load: %d", esp_get_free_heap_size());
  // do we need to load the epub?
  if (!epub || epub->get_path() != state.path)
  {
    renderer->show_busy();
    delete epub;
    delete parser;
    parser = nullptr;
    SET_CHECKPOINT(21);
    epub = new Epub(state.path);
    SET_CHECKPOINT(22);
    if (epub->load())
    {
      SET_CHECKPOINT(23);
      ESP_LOGD(TAG, "After epub load: %d", esp_get_free_heap_size());
      return false;
    }
  }
  return true;
}

void EpubReader::parse_and_layout_current_section()
{
  if (!parser)
  {
    SET_CHECKPOINT(60);
    renderer->show_busy();
    ESP_LOGI(TAG, "Parse and render section %d", state.current_section);
    ESP_LOGD(TAG, "Before read html: %d", esp_get_free_heap_size());

    // if spine item is not found here then it will return get_spine_item(0)
    // so it does not crashes when you want to go after last page (out of vector range)
    std::string item = epub->get_spine_item(state.current_section);
    std::string base_path = item.substr(0, item.find_last_of('/') + 1);
    SET_CHECKPOINT(61);
    char *html = reinterpret_cast<char *>(epub->get_item_contents(item));
    SET_CHECKPOINT(62);
    ESP_LOGD(TAG, "After read html: %d", esp_get_free_heap_size());
    if (html == nullptr)
    {
      ESP_LOGE(TAG, "Failed to read section html for %s", item.c_str());
      parser = new RubbishHtmlParser("", 0, base_path);
      state.pages_in_current_section = parser->get_page_count();
      return;
    }
    SET_CHECKPOINT(63);
    parser = new RubbishHtmlParser(html, strlen(html), base_path);
    free(html);
    SET_CHECKPOINT(64);
    ESP_LOGD(TAG, "After parse: %d", esp_get_free_heap_size());
    parser->layout(renderer, epub);
    SET_CHECKPOINT(65);
    ESP_LOGD(TAG, "After layout: %d", esp_get_free_heap_size());
    state.pages_in_current_section = parser->get_page_count();
    SET_CHECKPOINT(66);
  }
}

void EpubReader::next()
{
  state.current_page++;
  if (state.current_page >= state.pages_in_current_section)
  {
    state.current_section++;
    state.current_page = 0;
    delete parser;
    parser = nullptr;
  }
}

void EpubReader::prev()
{
  if (state.current_page == 0)
  {
    if (state.current_section > 0)
    {
      delete parser;
      parser = nullptr;
      state.current_section--;
      ESP_LOGD(TAG, "Going to previous section %d", state.current_section);
      parse_and_layout_current_section();
      state.current_page = state.pages_in_current_section - 1;
    }
    // Already at the first page of the first section; nothing to do.
    return;
  }
  state.current_page--;
}

void EpubReader::render()
{
  SET_CHECKPOINT(67);
  if (!parser)
  {
    parse_and_layout_current_section();
  }
  SET_CHECKPOINT(68);
  ESP_LOGD(TAG, "rendering page %d of %d", state.current_page, parser->get_page_count());
  parser->render_page(state.current_page, renderer, epub);
  SET_CHECKPOINT(69);
  ESP_LOGD(TAG, "rendered page %d of %d", state.current_page, parser->get_page_count());
  ESP_LOGD(TAG, "after render: %d", esp_get_free_heap_size());
}

void EpubReader::set_state_section(uint16_t current_section) {
  ESP_LOGI(TAG, "go to section:%d", current_section);
  state.current_section = current_section;
}