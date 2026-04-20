# AI Systems Engineering Interview Study Guide - PDF Generation

## Conversion Instructions

Since the automated conversion script requires additional dependencies (wkhtmltopdf), here are **three simple methods** to convert the study guide to PDF:

---

## Method 1: Using VS Code (Recommended - Easiest)

1. **Open the markdown file in VS Code:**
   ```bash
   code /Users/shaileshpilare/Documents/sentinel-fabric/docs/INTERVIEW_STUDY_GUIDE.md
   ```

2. **Open Command Palette:** `Cmd + Shift + P`

3. **Type:** "Markdown: Open Preview to the Side"

4. **In the preview pane,** right-click and select "Print" or press `Cmd + P`

5. **Choose "Save as PDF"** as the destination

6. **Click "Save"** and choose location: `docs/INTERVIEW_STUDY_GUIDE.pdf`

---

## Method 2: Using Browser (Quick)

1. **Open the markdown file in GitHub:**
   - Navigate to the file in your GitHub repository
   - OR open the HTML file if already generated

2. **Or open in browser directly:**
   ```bash
   open /Users/shaileshpilare/Documents/sentinel-fabric/docs/INTERVIEW_STUDY_GUIDE.html
   ```

3. **Press `Cmd + P`** (Print)

4. **Choose "Save as PDF"** from the destination dropdown

5. **Click "Save"**

---

## Method 3: Install Dependencies (For Automated Conversion)

If you want to use the automated script:

### Step 1: Install wkhtmltopdf
```bash
# Using Homebrew
brew install wkhtmltopdf
```

### Step 2: Install Python packages
```bash
pip3 install markdown pdfkit
```

### Step 3: Run the conversion script
```bash
cd /Users/shaileshpilare/Documents/sentinel-fabric
./scripts/convert_to_pdf.sh
```

---

## Method 4: Using Pandoc (Alternative)

### Step 1: Install Pandoc
```bash
brew install pandoc
```

### Step 2: Convert directly
```bash
cd /Users/shaileshpilare/Documents/sentinel-fabric
pandoc docs/INTERVIEW_STUDY_GUIDE.md -o docs/INTERVIEW_STUDY_GUIDE.pdf \
  --pdf-engine=xelatex \
  --variable geometry:margin=1in \
  --toc \
  --toc-depth=3
```

---

## Recommended Settings for PDF

When saving as PDF, use these settings for best results:

- **Page Size:** Letter (8.5" × 11")
- **Margins:** 0.75" on all sides
- **Orientation:** Portrait
- **Scale:** 100% (or "Fit to page" if available)
- **Headers/Footers:** Uncheck (to remove URLs and dates)
- **Background Graphics:** Check (for colored tables and highlights)

---

## File Locations

After conversion, you'll have:

- **Markdown:** `/Users/shaileshpilare/Documents/sentinel-fabric/docs/INTERVIEW_STUDY_GUIDE.md`
- **HTML:** `/Users/shaileshpilare/Documents/sentinel-fabric/docs/INTERVIEW_STUDY_GUIDE.html` (if generated)
- **PDF:** `/Users/shaileshpilare/Documents/sentinel-fabric/docs/INTERVIEW_STUDY_GUIDE.pdf` (after conversion)

---

## Quick Print Command (Mac)

For a quick PDF using Mac's built-in `cupsfilter`:

```bash
cd /Users/shaileshpilare/Documents/sentinel-fabric
cupsfilter docs/INTERVIEW_STUDY_GUIDE.md > docs/INTERVIEW_STUDY_GUIDE.pdf
```

Note: This may not preserve all formatting perfectly.

---

## Troubleshooting

### Issue: PDF is missing tables or formatting
**Solution:** Use Method 1 (VS Code) or Method 2 (Browser) for best formatting preservation

### Issue: PDF is too large (> 10MB)
**Solution:** 
- Reduce image quality in print settings
- Use "Optimize for Fast Web View" if available
- Compress with: `ghostscript -sDEVICE=pdfwrite -dCompatibilityLevel=1.4 -dPDFSETTINGS=/ebook -dNOPAUSE -dQUIET -dBATCH -sOutputFile=output.pdf input.pdf`

### Issue: Code blocks are cut off
**Solution:** 
- Choose "Fit to page" or scale to 90%
- Use Landscape orientation for pages with wide code blocks

---

## Estimated PDF Stats

Based on the markdown file:

- **Pages:** ~40-50 pages
- **File Size:** ~2-5 MB (depending on compression)
- **Word Count:** ~15,000 words
- **Reading Time:** 2-3 hours (full review)

---

## Next Steps

After creating the PDF:

1. **Review the PDF** to ensure all formatting is correct
2. **Bookmark key sections** (Phase 1-9, Resource Library, Progress Tracker)
3. **Print key pages** if you prefer physical copies (e.g., Progress Tracker, Weekly Checkpoints)
4. **Share with accountability partner** if studying with someone
5. **Load on tablet** for portable studying

---

**Good luck with your interview preparation!** 🚀
