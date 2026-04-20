#!/bin/bash
# Script to convert INTERVIEW_STUDY_GUIDE.md to PDF
# Uses Python with markdown and pdfkit libraries

echo "Converting INTERVIEW_STUDY_GUIDE.md to PDF..."

# Check if required Python packages are installed
python3 -c "import markdown" 2>/dev/null || {
    echo "Installing markdown package..."
    pip3 install markdown --quiet
}

python3 -c "import pdfkit" 2>/dev/null || {
    echo "Installing pdfkit package..."
    pip3 install pdfkit --quiet
}

# Check if wkhtmltopdf is installed
if ! command -v wkhtmltopdf &> /dev/null; then
    echo "wkhtmltopdf is not installed. Installing via Homebrew..."
    brew install wkhtmltopdf --quiet
fi

# Convert markdown to HTML first
python3 << 'EOF'
import markdown

# Read the markdown file
with open('docs/INTERVIEW_STUDY_GUIDE.md', 'r') as f:
    md_text = f.read()

# Convert to HTML with extensions
md = markdown.Markdown(extensions=['tables', 'fenced_code', 'toc', 'nl2br'])
html_body = md.convert(md_text)

# Create full HTML with CSS styling
html_template = f"""
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>AI Systems Engineering Interview Study Guide</title>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            line-height: 1.6;
            max-width: 900px;
            margin: 0 auto;
            padding: 40px 20px;
            color: #333;
        }}
        h1 {{
            color: #2c3e50;
            border-bottom: 3px solid #3498db;
            padding-bottom: 10px;
            font-size: 2.5em;
        }}
        h2 {{
            color: #34495e;
            border-bottom: 2px solid #ecf0f1;
            padding-bottom: 8px;
            margin-top: 40px;
            font-size: 1.8em;
        }}
        h3 {{
            color: #7f8c8d;
            margin-top: 30px;
            font-size: 1.4em;
        }}
        table {{
            border-collapse: collapse;
            width: 100%;
            margin: 20px 0;
            font-size: 0.95em;
        }}
        th, td {{
            border: 1px solid #bdc3c7;
            padding: 12px;
            text-align: left;
        }}
        th {{
            background-color: #3498db;
            color: white;
            font-weight: 600;
        }}
        tr:nth-child(even) {{
            background-color: #ecf0f1;
        }}
        code {{
            background-color: #f8f9fa;
            padding: 2px 6px;
            border-radius: 3px;
            font-family: 'Courier New', Courier, monospace;
            color: #e74c3c;
        }}
        pre {{
            background-color: #2c3e50;
            color: #ecf0f1;
            padding: 15px;
            border-radius: 5px;
            overflow-x: auto;
            font-size: 0.9em;
        }}
        blockquote {{
            border-left: 4px solid #3498db;
            margin: 20px 0;
            padding-left: 20px;
            color: #7f8c8d;
            font-style: italic;
        }}
        ul, ol {{
            margin: 15px 0;
            padding-left: 30px;
        }}
        li {{
            margin: 8px 0;
        }}
        .priority-p0 {{
            background-color: #ffebee;
            border-left: 4px solid #e74c3c;
            padding: 10px;
            margin: 15px 0;
        }}
        .priority-p1 {{
            background-color: #fff3e0;
            border-left: 4px solid #f39c12;
            padding: 10px;
            margin: 15px 0;
        }}
        .priority-p2 {{
            background-color: #e8f5e9;
            border-left: 4px solid #27ae60;
            padding: 10px;
            margin: 15px 0;
        }}
        a {{
            color: #3498db;
            text-decoration: none;
        }}
        a:hover {{
            text-decoration: underline;
        }}
        @media print {{
            body {{
                max-width: 100%;
                padding: 20px;
            }}
            h1 {{
                font-size: 2em;
            }}
            h2 {{
                font-size: 1.6em;
            }}
            pre {{
                font-size: 0.8em;
                page-break-inside: avoid;
            }}
            table {{
                font-size: 0.85em;
            }}
        }}
    </style>
</head>
<body>
{html_body}
</body>
</html>
"""

# Write HTML to file
with open('docs/INTERVIEW_STUDY_GUIDE.html', 'w') as f:
    f.write(html_template)

print("HTML file created: docs/INTERVIEW_STUDY_GUIDE.html")
EOF

# Convert HTML to PDF
python3 << 'EOF'
import pdfkit

# Convert HTML to PDF with options
options = {
    'page-size': 'Letter',
    'margin-top': '0.75in',
    'margin-right': '0.75in',
    'margin-bottom': '0.75in',
    'margin-left': '0.75in',
    'encoding': 'UTF-8',
    'custom-header': [
        ('Accept-Encoding', 'gzip')
    ],
    'quiet': '',
    'enable-local-file-access': '',
}

try:
    pdfkit.from_file('docs/INTERVIEW_STUDY_GUIDE.html', 'docs/INTERVIEW_STUDY_GUIDE.pdf', options=options)
    print("✓ PDF created successfully: docs/INTERVIEW_STUDY_GUIDE.pdf")
except Exception as e:
    print(f"✗ Error converting to PDF: {e}")
    print("\nAlternative: Open the HTML file in your browser and print to PDF manually:")
    print("  open docs/INTERVIEW_STUDY_GUIDE.html")
EOF

echo ""
echo "Conversion complete!"
echo "Output files:"
echo "  - docs/INTERVIEW_STUDY_GUIDE.html (HTML version)"
echo "  - docs/INTERVIEW_STUDY_GUIDE.pdf (PDF version)"
