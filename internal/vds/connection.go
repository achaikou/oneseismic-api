package vds

import (
	"fmt"
	"strings"
	"net/url"
)

type Connection interface {
	Url()              string
	ConnectionString() string
}

type AzureConnection struct {
	blobPath  string
	container string
	host      string
	sas       string
}

func (c *AzureConnection) Url() string {
	return fmt.Sprintf("azure://%s/%s", c.container, c.blobPath)
}

func (c *AzureConnection) ConnectionString() string {
	return fmt.Sprintf("BlobEndpoint=https://%s;SharedAccessSignature=?%s",
		c.host,
		c.sas,
	)
}

func NewAzureConnection(
	blobPath  string,
	container string,
	host      string,
	sas       string,
) *AzureConnection {
	return &AzureConnection{
		blobPath:  blobPath,
		container: container,
		host:      host,
		sas:       sas,
	}
}

type FileConnection struct {
	url string
}

func (f *FileConnection) Url() string {
	return f.url
}

func (f *FileConnection) ConnectionString() string {
	return ""
}

func NewFileConnection(path string) *FileConnection {
	return &FileConnection{ url: path }
}

/*
 * Sanitize path and make the input path into a *url.URL type
 *
 * OpenVDS segfaults if the blobpath ends on a trailing slash. We don't want to
 * propagate that behaviour to the user in any way, so we need to explicitly
 * sanitize the input and strip trailing slashes before passing them on to
 * OpenVDS.
 */
func makeUrl(path string) (*url.URL, error) {
	path = strings.TrimSuffix(path, "/")
	return url.Parse(path)
}

func isAllowed(allowlist []*url.URL, requested *url.URL) error {
	for _, candidate := range allowlist {
		if strings.EqualFold(requested.Host, candidate.Host) {
			return nil
		}
	}
	msg := "unsupported storage account: %s. This API is configured to work "  +
		"with a pre-defined set of storage accounts. Contact the system admin " +
		"to get your storage account on the allowlist"
	return fmt.Errorf(msg, requested.Host)
}
/*
 * Strip leading ? if present from the input SAS token
 */
func sanitizeSAS(sas string) string {
	return strings.TrimPrefix(sas, "?")
}

func splitAzureUrl(path string) (string, string) {
	path = strings.TrimPrefix(path, "/")
	container, blobPath, _ := strings.Cut(path, "/")
	return container, blobPath
}

type ConnectionMaker func(blob, sas string) (Connection, error)

func MakeAzureConnection(accounts []string) ConnectionMaker {
	var allowlist []*url.URL
	for _, account := range accounts {
		account = strings.TrimSpace(account)
		if len(account) == 0 {
			panic("Empty storage-account not allowed")
		}
		url, err := url.Parse(account)
		if err != nil {
			panic(err)
		}

		allowlist = append(allowlist, url)
	}

	return func(blob string, sas string) (Connection, error) {
		blobUrl, err := makeUrl(blob)
		if err != nil {
			return nil, err
		}

		if err := isAllowed(allowlist, blobUrl); err != nil {
			return nil, err
		}

		container, blobPath := splitAzureUrl(blobUrl.Path)
		return NewAzureConnection(blobPath, container, blobUrl.Host, sanitizeSAS(sas)), nil
	}
}
