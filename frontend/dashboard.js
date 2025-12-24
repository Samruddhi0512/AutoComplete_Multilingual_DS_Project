const API = "http://127.0.0.1:9090/stats";

// -----------------------------
// Check script/language type
// -----------------------------

function isDevanagari(word) {
    if (!word) return false;
    let cp = word.codePointAt(0);
    return cp >= 0x0900 && cp <= 0x097F;
}

function isEnglish(word) {
    if (!word) return false;
    return /^[A-Za-z]/.test(word);
}

// -----------------------------
// Fill a table with top 3 words
// -----------------------------
function fillTable(tableId, items) {
    const table = document.getElementById(tableId);

    items.forEach((item, index) => {
        const tr = document.createElement("tr");

        tr.innerHTML = `
            <td>${index + 1}</td>
            <td>${item.word}</td>
            <td>${item.freq}</td>
        `;

        table.appendChild(tr);
    });
}

// -----------------------------
// Main function: fetch + process
// -----------------------------
async function loadData() {
    try {
        const res = await fetch(API);
        const data = await res.json();

        let hindi = [];
        let marathi = [];
        let english = [];

        // Separate based on script
        data.forEach(item => {
            let w = item.word;
            let f = item.freq;

            if (f === 0) return;   // ignore unused words

            if (isDevanagari(w)) {
                // We don't have separate detection for Marathi vs Hindi
                // So this simple rule will split them:
                if (w.length <= 3) hindi.push(item);
                else marathi.push(item);
            }
            else if (isEnglish(w)) {
                english.push(item);
            }
        });

        // Sort by frequency descending
        hindi.sort((a, b) => b.freq - a.freq);
        marathi.sort((a, b) => b.freq - a.freq);
        english.sort((a, b) => b.freq - a.freq);

        // Take top 3
        fillTable("hindiTable", hindi.slice(0, 3));
        fillTable("marathiTable", marathi.slice(0, 3));
        fillTable("englishTable", english.slice(0, 3));
    }
    catch (error) {
        console.error("Error loading stats:", error);
    }
}

loadData();
