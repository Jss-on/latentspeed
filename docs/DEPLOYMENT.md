# Documentation Deployment Guide

This guide explains how to deploy the Latentspeed Trading Engine documentation to GitHub Pages.

## Automatic Deployment (Recommended)

The repository is configured for automatic documentation deployment using GitHub Actions.

### Setup Steps

1. **Push your code to GitHub** (if not already done):
   ```bash
   git add .
   git commit -m "Add comprehensive Doxygen documentation"
   git push origin main
   ```

2. **Enable GitHub Pages** in your repository:
   - Go to your repository on GitHub
   - Navigate to **Settings** → **Pages**
   - Under **Source**, select **GitHub Actions**
   - Save the settings

3. **Trigger deployment**:
   - The workflow will automatically trigger on pushes to `main`/`master` branch
   - Or manually trigger from **Actions** tab → **Build and Deploy Documentation** → **Run workflow**

4. **Access your documentation**:
   - Once deployed, your docs will be available at:
   - `https://<your-username>.github.io/<repository-name>/`
   - Example: `https://jss-on.github.io/latentspeed/`

### Workflow Features

- **Automatic building**: Installs Doxygen and generates docs on every push
- **Dependency management**: Automatically installs required tools (Doxygen, Graphviz)
- **Error handling**: Fails gracefully if documentation generation fails
- **Concurrent deployment**: Prevents conflicts during deployment
- **Path-based triggers**: Only runs when relevant files change

### Monitoring

- Check the **Actions** tab for build/deployment status
- View logs for any build failures
- Monitor deployment progress in real-time

## Manual Deployment (Alternative)

If you prefer manual control:

### Build Documentation Locally

```bash
# Install Doxygen (Ubuntu/Debian)
sudo apt-get install doxygen graphviz

# Or on macOS
brew install doxygen graphviz

# Generate documentation
doxygen Doxyfile
```

### Deploy to GitHub Pages

1. **Build documentation locally**:
   ```bash
   doxygen Doxyfile
   ```

2. **Use gh-pages branch**:
   ```bash
   # Create and switch to gh-pages branch
   git checkout --orphan gh-pages
   
   # Copy generated docs to root
   cp -r docs/html/* .
   
   # Add and commit
   git add .
   git commit -m "Deploy documentation"
   
   # Push to gh-pages branch
   git push origin gh-pages
   ```

3. **Configure GitHub Pages**:
   - Go to **Settings** → **Pages**
   - Select **Deploy from branch**
   - Choose **gh-pages** branch
   - Select **/ (root)** folder

## Documentation Structure

```
docs/
├── html/                   # Generated HTML documentation
│   ├── index.html         # Main documentation page
│   ├── classes.html       # Class list
│   ├── files.html         # File list
│   ├── namespaces.html    # Namespace documentation
│   └── ...               # Additional generated files
└── DEPLOYMENT.md          # This file
```

## Customization

### Styling

To customize documentation appearance:

1. **Edit Doxyfile**:
   ```
   HTML_EXTRA_STYLESHEET = docs/custom.css
   HTML_COLORSTYLE_HUE = 220
   HTML_COLORSTYLE_SAT = 100
   ```

2. **Create custom CSS**:
   ```bash
   # Create custom stylesheet
   touch docs/custom.css
   ```

### Logo and Branding

Add your project logo:

```
PROJECT_LOGO = docs/logo.png
```

## Troubleshooting

### Common Issues

1. **Documentation not generating**:
   - Check that all source files have proper Doxygen comments
   - Verify file paths in Doxyfile INPUT section

2. **GitHub Pages not updating**:
   - Check Actions tab for build failures
   - Ensure GitHub Pages is enabled with correct source

3. **Missing graphs/diagrams**:
   - Install Graphviz: `sudo apt-get install graphviz`
   - Enable in Doxyfile: `HAVE_DOT = YES`

### Debugging

Enable verbose output in Doxyfile:
```
QUIET = NO
WARNINGS = YES
WARN_IF_UNDOCUMENTED = YES
```

## Advanced Features

### Search Functionality

Enable search in generated docs:
```
SEARCHENGINE = YES
SERVER_BASED_SEARCH = NO
```

### LaTeX/PDF Output

Generate PDF documentation:
```
GENERATE_LATEX = YES
USE_PDFLATEX = YES
PDF_HYPERLINKS = YES
```

### Integration with CI/CD

The workflow can be extended for additional checks:

```yaml
- name: Check Documentation Quality
  run: |
    # Count documented vs undocumented items
    doxygen Doxyfile 2>&1 | grep -E "(warning|error)" || true
    
    # Generate coverage report
    echo "Documentation coverage analysis..."
```

## Maintenance

### Regular Updates

- Documentation updates automatically when code changes
- Review generated docs periodically for accuracy
- Update Doxyfile configuration as project evolves

### Performance

- Large codebases may need optimization:
  ```
  DOT_GRAPH_MAX_NODES = 50
  MAX_DOT_GRAPH_DEPTH = 2
  ```

For questions or issues, refer to:
- [Doxygen Documentation](https://www.doxygen.nl/manual/)
- [GitHub Pages Guide](https://docs.github.com/en/pages)
- [GitHub Actions Documentation](https://docs.github.com/en/actions)
