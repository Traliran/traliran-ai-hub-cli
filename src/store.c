/*
 * store.c - assistant store (free presets + paid marketplace links).
 */
#include <stdlib.h>
#include <string.h>
#include "store.h"

static store_item_t FREE_ITEMS[] = {
    {
        "Polyglot Translator",
        "Translates incoming texts into target language systems maintaining proper cultural idioms.",
        NULL,
        "You are an advanced expert translator. Your sole purpose is to translate any text provided into high-quality, fluent English (or the language specified by user), keeping idioms and contextual tone perfectly intact. Avoid chatbot greetings."
    },
    {
        "Code & Text Editor",
        "Corrects syntax structural flaws, styles code, or fixes grammar blocks instantly.",
        NULL,
        "You are a professional editor. Analyze the user text or source code payload, fix bugs, optimize layout structures, polish grammatical issues, and return the cleaned output without conversational filler."
    },
    {
        "Ideation Generator",
        "Brainstorms creative concepts, scripts, plot designs, and mechanics loops.",
        NULL,
        "You are a creative brainstorming engine. Generate out-of-the-box ideas, architectural blueprints, script writing angles, and unique strategic concepts based on user prompts. Deliver responses using highly structured bullet points."
    },
};

static store_item_t PAID_ITEMS[] = {
    { "Markdown assistant - $5", "Automates knowledge management (Zettelkasten / P.A.R.A.) by converting unformatted text into structured Markdown for Obsidian or Notion.", "https://whop.com/joined/traliran-ai-huub/products/markdown-assistant/", NULL },
    { "Text Game Master - $5", "An AI-powered lore keeper and consistency editor for writers that ensures narrative logic and internal world rules are followed.", "https://whop.com/joined/traliran-ai-huub/products/text-game-master-co-writer-for-authors/", NULL },
    { "Short-Form Video Scriptwriter - $5", "AI marketing strategist generating high-retention scripts for TikTok, Reels, and Shorts.", "https://whop.com/joined/traliran-ai-huub/products/short-form-video-scriptwriter/", NULL },
    { "Mind-Map & Pin-Card Designer - $5", "Analytical AI for transforming raw data into logical, hierarchical diagrams.", "https://whop.com/joined/traliran-ai-huub/products/mind-map-pin-card-designer/", NULL },
    { "Deep Script Analyst - $4", "Structural/narrative analytics for screenwriters: pacing, cohesion, character arc metrics.", "https://whop.com/joined/traliran-ai-huub/products/deep-script-analyst/", NULL },
    { "AI Knowledge Auditor - $6", "Adaptive Socratic-style testing that audits your actual understanding of saved materials.", "https://whop.com/joined/traliran-ai-huub/products/ai-knowledge-auditor/", NULL },
    { "Fact Only AI - $4", "Never hallucinates. Only verified facts, or an honest: I don't know.", "https://whop.com/joined/traliran-ai-huub/products/fact-only-ai/", NULL },
    { "BrainSpark AI - $5", "Interactive brainstorming partner that sorts ideas by originality and feasibility.", "https://whop.com/joined/traliran-ai-huub/products/brainspark-ai-48/", NULL },
    { "LearnMate AI - $5", "Tutor that tests your level, builds a learning plan, and guides you with lessons and quizzes.", "https://whop.com/joined/traliran-ai-huub/products/learnmate-ai/", NULL },
    { "ResumeVerity AI - $5", "Compares a resume with an interview transcript to expose inconsistencies.", "https://whop.com/traliran-ai-huub/resumeverity-ai-the-candidate-honesty-detector/", NULL },
    { "MindEase AI - $5", "Empathetic conversational companion with evidence-based coping tools.", "https://whop.com/traliran-ai-huub/mindease-ai-your-compassionate-conversational-companion/", NULL },
    { "HireMap AI - $5", "Expert hiring advice and personalized recruitment action maps.", "https://whop.com/traliran-ai-huub/hiremap-ai-the-recruitment-strategist-interactive-action-planner/", NULL },
    { "MarketViz AI - $5", "Marketing advice and text-based charts right in the chat.", "https://whop.com/traliran-ai-huub/marketviz-ai-the-marketing-strategist-with-built-in-data-visualization/", NULL },
};

#define NFREE ((int)(sizeof(FREE_ITEMS) / sizeof(FREE_ITEMS[0])))
#define NPAID ((int)(sizeof(PAID_ITEMS) / sizeof(PAID_ITEMS[0])))

void store_init(store_t *s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < NFREE; i++) {
        s->free = realloc(s->free, sizeof(store_item_t *) * (size_t)(s->nfree + 1));
        s->free[s->nfree++] = &FREE_ITEMS[i];
    }
    for (int i = 0; i < NPAID; i++) {
        s->paid = realloc(s->paid, sizeof(store_item_t *) * (size_t)(s->npaid + 1));
        s->paid[s->npaid++] = &PAID_ITEMS[i];
    }
}