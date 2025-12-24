// =========================
// FRONTEND JAVASCRIPT (FINAL VERSION)
// =========================

// Backend API URL
const API = "http://127.0.0.1:9090";

// Getting HTML elements
const typed = document.getElementById("typed");
const chips = document.getElementById("chips");
const popup = document.getElementById("popup");

// NEW spinner reference
const spinnerEl = document.getElementById("spinner");

// Debounce + keyboard navigation
let timer = null;
let selectedIndex = -1;

// ===============================================
//  1. LOCALSTORAGE HISTORY SYSTEM
// ===============================================

// Load history array from localStorage
function loadHistory() {
    return JSON.parse(localStorage.getItem("history") || "[]");
}

// Save updated history
function saveHistory(list) {
    localStorage.setItem("history", JSON.stringify(list));
}

// Add new word to history
function addToHistory(word) {
    let history = loadHistory();

    // remove duplicate
    history = history.filter(w => w !== word);

    // add new on top
    history.unshift(word);

    // keep max 15
    if (history.length > 15) history.pop();

    saveHistory(history);
}

// Return history suggestions that start with prefix
function getHistoryMatches(prefix) {
    let history = loadHistory();
    return history.filter(w => w.startsWith(prefix));
}

// ===============================================
// 🔥 2. SPINNER HELPERS
// ===============================================
function showSpinner() {
    spinnerEl.classList.remove("hidden");
    spinnerEl.setAttribute("aria-hidden", "false");
}

function hideSpinner() {
    spinnerEl.classList.add("hidden");
    spinnerEl.setAttribute("aria-hidden", "true");
}

// ===============================================
// 3. DEBOUNCE FUNCTION
// ===============================================
function debounce(fn, ms = 120) {
    return (...args) => {
        clearTimeout(timer);
        timer = setTimeout(() => fn(...args), ms);
    };
}

// ===============================================
// 4. FETCH SUGGESTIONS (API + LOCAL HISTORY)
// ===============================================
async function fetchSuggestions(fullText) {
    const words = fullText.trim().split(" ");
    const prefix = words[words.length - 1];

    if (!prefix) {
        chips.innerHTML = "";
        hideSpinner();
        return;
    }

    // Show modern loading spinner
    showSpinner();

    try {
        const res = await fetch(`${API}/suggest?prefix=${encodeURIComponent(prefix)}`);
        const data = await res.json();

        // Combine backend + local suggestions
        renderChips(fullText, prefix, data.slice(0, 5));

    } catch {
        chips.innerHTML = "<div class='chip'>⚠️ Server Offline</div>";
    } finally {
        hideSpinner();
    }
}

// ===============================================
// 5. RENDER CHIPS (HISTORY FIRST, THEN API)
// ===============================================
function renderChips(fullText, prefix, apiList) {
    chips.innerHTML = "";
    selectedIndex = -1;

    const words = fullText.trim().split(" ");
    words.pop(); // remove typed part

    let localMatches = getHistoryMatches(prefix);

    // Avoid duplicate words
    let finalList = [...localMatches, ...apiList.filter(w => !localMatches.includes(w))];

    // Maximum 5 chips
    finalList = finalList.slice(0, 5);

    finalList.forEach((word) => {
        const div = document.createElement("div");
        div.className = "chip";

        // Highlight typed prefix
        div.innerHTML =
            `<strong>${word.substring(0, prefix.length)}</strong>` +
            word.substring(prefix.length);

        div.onclick = () => applySuggestion(word, words, div);

        chips.appendChild(div);
    });
}

// ===============================================
// 6. APPLY SUGGESTION (INSERT + HISTORY + BACKEND)
// ===============================================
function applySuggestion(word, words, div) {
    // Glow animation
    div.classList.add("glow");

    // Insert selected word
    words.push(word);
    typed.value = words.join(" ") + " ";

    chips.innerHTML = "";

    // Save to local history
    addToHistory(word);

    // Send word to backend
    fetch(`${API}/select`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ word }),
    });
}

// ===============================================
// 7. KEYBOARD NAVIGATION
// ===============================================
typed.addEventListener("keydown", (e) => {
    const items = Array.from(chips.children);
    if (items.length === 0) return;

    if (e.key === "ArrowDown") {
        selectedIndex = (selectedIndex + 1) % items.length;
        updateChipSelection(items);
        e.preventDefault();
    } else if (e.key === "ArrowUp") {
        selectedIndex = (selectedIndex - 1 + items.length) % items.length;
        updateChipSelection(items);
        e.preventDefault();
    } else if (e.key === "Enter" && selectedIndex >= 0) {
        items[selectedIndex].click();
        e.preventDefault();
    }
});

function updateChipSelection(items) {
    items.forEach((chip, i) => {
        chip.style.background = i === selectedIndex ? "#b24cff" : "";
        chip.style.color = i === selectedIndex ? "white" : "";
    });
}

// ===============================================
// 8. DARK MODE
// ===============================================
document.getElementById("themeToggle").onclick = () => {
    document.body.classList.toggle("dark");
};

// ===============================================
// 9. INPUT EVENT (AUTOCOMPLETE)
// ===============================================
typed.addEventListener(
    "input",
    debounce((e) => {
        fetchSuggestions(e.target.value);
    })
);
